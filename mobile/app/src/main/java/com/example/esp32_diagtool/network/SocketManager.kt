package com.example.esp32_diagtool.network

import android.util.Log
import com.example.esp32_diagtool.model.GpioPacket
import com.example.esp32_diagtool.model.ImuStreamPacket
import com.example.esp32_diagtool.model.TelemetryPacket
import java.nio.charset.StandardCharsets
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString

class SocketManager(
    private val onImuDataReceived: (ImuStreamPacket) -> Unit,
    private val onTelemetryDataReceived: (TelemetryPacket) -> Unit,
    private val onGpioDataReceived: (GpioPacket) -> Unit,
    private val onConnectionChanged: (Boolean) -> Unit
) {
    companion object {
        private const val TAG = "SocketManager"
        private const val HOST = "192.168.4.1"
        private const val PORT = 3333
        private const val WS_PATH = "/"
        private const val IMU_PACKET_HEADER = 0xA1
        private const val TELEMETRY_PACKET_HEADER = 0xD4
        private const val GPIO_PACKET_HEADER = 0xC1
        private const val IMU_PAYLOAD_SIZE_BYTES = 20
        private const val IMU_PACKET_SIZE_BYTES = 1 + IMU_PAYLOAD_SIZE_BYTES
        private const val TELEMETRY_PAYLOAD_SIZE_BYTES = 21
        private const val TELEMETRY_PACKET_SIZE_BYTES = 1 + TELEMETRY_PAYLOAD_SIZE_BYTES
        private const val GPIO_PAYLOAD_SIZE_BYTES = 17
        private const val GPIO_PACKET_SIZE_BYTES = 1 + GPIO_PAYLOAD_SIZE_BYTES
        private const val CONNECT_TIMEOUT_MS = 5000
        private const val RECONNECT_DELAY_MS = 1500L
    }

    @Volatile
    private var isRunning = false

    @Volatile
    private var isConnected = false

    @Volatile
    private var webSocket: WebSocket? = null

    @Volatile
    private var pendingBytes: ByteArray = ByteArray(0)

    @Volatile
    private var rawFrameCount: Long = 0

    @Volatile
    private var imuPacketCount: Long = 0

    @Volatile
    private var telemetryPacketCount: Long = 0

    @Volatile
    private var gpioPacketCount: Long = 0

    private var workerThread: Thread? = null

    private val wsClient = OkHttpClient.Builder()
        .connectTimeout(CONNECT_TIMEOUT_MS.toLong(), TimeUnit.MILLISECONDS)
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .build()

    fun connect() {
        if (isRunning && workerThread?.isAlive == true) return
        isRunning = true
        val wsUrl = "ws://$HOST:$PORT$WS_PATH"
        workerThread = Thread {
            while (isRunning) {
                val sessionClosed = CountDownLatch(1)
                try {
                    val request = Request.Builder()
                        .url(wsUrl)
                        .build()

                    val listener = object : WebSocketListener() {
                        override fun onOpen(webSocket: WebSocket, response: Response) {
                            this@SocketManager.webSocket = webSocket
                            pendingBytes = ByteArray(0)
                            rawFrameCount = 0
                            imuPacketCount = 0
                            telemetryPacketCount = 0
                            gpioPacketCount = 0
                            Log.d(TAG, "Connected to $wsUrl")
                            updateConnectionState(true)
                        }

                        override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                            rawFrameCount++
                            if (rawFrameCount <= 3L || rawFrameCount % 50L == 0L) {
                                Log.d(TAG, "Binary frame #$rawFrameCount bytes=${bytes.size}")
                            }
                            readPackets(bytes.toByteArray())
                        }

                        override fun onMessage(webSocket: WebSocket, text: String) {
                            rawFrameCount++
                            if (rawFrameCount <= 3L || rawFrameCount % 50L == 0L) {
                                Log.d(TAG, "Text frame #$rawFrameCount chars=${text.length}")
                            }
                            // Some firmware builds send telemetry as text frames.
                            val payload = textFrameToBytes(text)
                            if (payload.isNotEmpty()) {
                                readPackets(payload)
                            } else {
                                Log.w(TAG, "Dropped empty text payload")
                            }
                        }

                        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                            webSocket.close(1000, null)
                        }

                        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                            if (isRunning) {
                                Log.w(TAG, "WebSocket closed: code=$code, reason=$reason")
                            }
                            clearWebSocket(webSocket)
                            updateConnectionState(false)
                            sessionClosed.countDown()
                        }

                        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                            if (isRunning) {
                                Log.e(TAG, "WebSocket error: ${t.message}", t)
                            }
                            clearWebSocket(webSocket)
                            updateConnectionState(false)
                            sessionClosed.countDown()
                        }
                    }

                    val activeSocket = wsClient.newWebSocket(request, listener)
                    webSocket = activeSocket

                    while (isRunning && !isConnected) {
                        Thread.sleep(50)
                    }

                    if (!isRunning) {
                        activeSocket.close(1000, "Client disconnect")
                        sessionClosed.countDown()
                    }

                    sessionClosed.await()
                } catch (e: Exception) {
                    if (isRunning) {
                        Log.e(TAG, "Connection loop error: ${e.message}", e)
                    }
                } finally {
                    closeWebSocket()
                    updateConnectionState(false)
                }

                if (isRunning) {
                    try {
                        Thread.sleep(RECONNECT_DELAY_MS)
                    } catch (_: InterruptedException) {
                        Thread.currentThread().interrupt()
                    }
                }
            }
        }.apply { 
            name = "SocketManager-Reader"
            start() 
        }
    }

    fun disconnect() {
        isRunning = false
        closeWebSocket()
        workerThread?.interrupt()
        workerThread = null
        pendingBytes = ByteArray(0)
        updateConnectionState(false)
    }

    @Synchronized
    private fun readPackets(data: ByteArray) {
        if (data.isEmpty()) return
        val merged = if (pendingBytes.isEmpty()) data else pendingBytes + data
        var index = 0
        var unknownHeaderCount = 0

        try {
            while (index < merged.size) {
                val header = merged[index].toInt() and 0xFF
                when (header) {
                    IMU_PACKET_HEADER -> {
                        if (index + IMU_PACKET_SIZE_BYTES > merged.size) {
                            break
                        }

                        val bb = ByteBuffer
                            .wrap(merged, index + 1, IMU_PAYLOAD_SIZE_BYTES)
                            .order(ByteOrder.LITTLE_ENDIAN)

                        val packet = ImuStreamPacket(
                            header = IMU_PACKET_HEADER.toShort(),
                            sequence = bb.int.toLong() and 0xFFFFFFFFL,
                            sampleMicros = bb.int.toLong() and 0xFFFFFFFFL,
                            gyroX = bb.short,
                            gyroY = bb.short,
                            gyroZ = bb.short,
                            accelX = bb.short,
                            accelY = bb.short,
                            accelZ = bb.short
                        )
                        imuPacketCount++
                        if (imuPacketCount <= 3L || imuPacketCount % 100L == 0L) {
                            Log.d(
                                TAG,
                                "IMU #$imuPacketCount seq=${packet.sequence} sampleUs=${packet.sampleMicros} gx=${packet.gyroX} gy=${packet.gyroY} gz=${packet.gyroZ}"
                            )
                        }
                        onImuDataReceived(packet)
                        index += IMU_PACKET_SIZE_BYTES
                    }

                    TELEMETRY_PACKET_HEADER -> {
                        if (index + TELEMETRY_PACKET_SIZE_BYTES > merged.size) {
                            break
                        }

                        val bb = ByteBuffer
                            .wrap(merged, index + 1, TELEMETRY_PAYLOAD_SIZE_BYTES)
                            .order(ByteOrder.LITTLE_ENDIAN)

                        val packet = TelemetryPacket(
                            header = TELEMETRY_PACKET_HEADER.toShort(),
                            sampleMs = bb.int.toLong() and 0xFFFFFFFFL,
                            temp = bb.float,
                            volt = bb.float,
                            curr = bb.float,
                            bat = bb.get().toInt() and 0xFF,
                            cpu = bb.get().toInt() and 0xFF,
                            rtcHour = bb.get().toInt() and 0xFF,
                            rtcMin = bb.get().toInt() and 0xFF,
                            rtcSec = bb.get().toInt() and 0xFF
                        )
                        telemetryPacketCount++
                        if (telemetryPacketCount <= 3L || telemetryPacketCount % 50L == 0L) {
                            Log.d(
                                TAG,
                                "TEL #$telemetryPacketCount ms=${packet.sampleMs} temp=${packet.temp} volt=${packet.volt} curr=${packet.curr} bat=${packet.bat} cpu=${packet.cpu} rtc=${packet.rtcHour}:${packet.rtcMin}:${packet.rtcSec}"
                            )
                        }
                        onTelemetryDataReceived(packet)
                        index += TELEMETRY_PACKET_SIZE_BYTES
                    }

                    GPIO_PACKET_HEADER -> {
                        if (index + GPIO_PACKET_SIZE_BYTES > merged.size) {
                            break
                        }

                        val bb = ByteBuffer
                            .wrap(merged, index + 1, GPIO_PAYLOAD_SIZE_BYTES)
                            .order(ByteOrder.LITTLE_ENDIAN)

                        val packet = GpioPacket(
                            header = GPIO_PACKET_HEADER.toShort(),
                            gpio4 = bb.get().toInt() and 0xFF,
                            gpio3 = bb.get().toInt() and 0xFF,
                            gpio2 = bb.get().toInt() and 0xFF,
                            gpio1 = bb.get().toInt() and 0xFF,
                            gpio0 = bb.get().toInt() and 0xFF,
                            gpio21 = bb.get().toInt() and 0xFF,
                            gpio20 = bb.get().toInt() and 0xFF,
                            gpio10 = bb.get().toInt() and 0xFF,
                            gpio9 = bb.get().toInt() and 0xFF,
                            gpio8 = bb.get().toInt() and 0xFF,
                            gpio7 = bb.get().toInt() and 0xFF,
                            gpio6 = bb.get().toInt() and 0xFF,
                            gpio5 = bb.get().toInt() and 0xFF,
                            thermistorIsConnected = bb.get().toInt() and 0xFF,
                            i2cInaIsConnected = bb.get().toInt() and 0xFF,
                            i2cRtcIsConnected = bb.get().toInt() and 0xFF,
                            i2cGyroIsConnected = bb.get().toInt() and 0xFF
                        )
                        gpioPacketCount++
                        if (gpioPacketCount <= 3L || gpioPacketCount % 50L == 0L) {
                            Log.d(
                                TAG,
                                "GPIO #$gpioPacketCount g4=${packet.gpio4} g3=${packet.gpio3} g2=${packet.gpio2} g1=${packet.gpio1} g0=${packet.gpio0} g21=${packet.gpio21} g20=${packet.gpio20} g10=${packet.gpio10} g9=${packet.gpio9} g8=${packet.gpio8} g7=${packet.gpio7} g6=${packet.gpio6} g5=${packet.gpio5} therm=${packet.thermistorIsConnected} ina=${packet.i2cInaIsConnected} rtc=${packet.i2cRtcIsConnected} gyro=${packet.i2cGyroIsConnected}"
                            )
                        }
                        onGpioDataReceived(packet)
                        index += GPIO_PACKET_SIZE_BYTES
                    }

                    else -> {
                        unknownHeaderCount++
                        index++
                    }
                }
            }

            pendingBytes = if (index < merged.size) merged.copyOfRange(index, merged.size) else ByteArray(0)
            if (unknownHeaderCount > 0) {
                Log.w(TAG, "Skipped $unknownHeaderCount unknown header bytes in chunk=${merged.size}, pending=${pendingBytes.size}")
            }
            if (pendingBytes.isNotEmpty() && pendingBytes.size > 256) {
                Log.w(TAG, "Large pending buffer size=${pendingBytes.size}")
            }
        } catch (t: Throwable) {
            Log.e(TAG, "Packet parse error: ${t.message}", t)
            pendingBytes = ByteArray(0)
        }
    }

    private fun textFrameToBytes(text: String): ByteArray {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return ByteArray(0)

        val compactHex = trimmed.replace(" ", "")
        if (compactHex.length % 2 == 0 && compactHex.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) {
            return ByteArray(compactHex.length / 2) { i ->
                compactHex.substring(i * 2, i * 2 + 2).toInt(16).toByte()
            }
        }

        return trimmed.toByteArray(StandardCharsets.ISO_8859_1)
    }

    private fun clearWebSocket(target: WebSocket) {
        if (webSocket === target) {
            webSocket = null
        }
    }

    private fun updateConnectionState(connected: Boolean) {
        if (isConnected != connected) {
            isConnected = connected
            onConnectionChanged(connected)
        }
    }

    private fun closeWebSocket() {
        try {
            webSocket?.close(1000, "Client disconnect")
        } catch (_: Exception) {
        } finally {
            webSocket = null
        }
    }
}

package com.example.esp32_diagtool.mqtt

import android.util.Log
import com.google.gson.Gson
import com.example.esp32_diagtool.model.EspData
import org.eclipse.paho.client.mqttv3.*
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.security.SecureRandom
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

class MqttManager(
    private val onDataReceived: (EspData) -> Unit,
    private val onConnectionChanged: (Boolean) -> Unit
) {
    companion object {
        private const val TAG = "MqttManager"
        private const val BROKER_URI = "ssl://13c35f0a5bde4564be3ea561c26c7c3b.s1.eu.hivemq.cloud:8883"
        private const val USERNAME = "esp32mqtt"
        private const val PASSWORD = "L7sqBG*9+w2m"
        private const val TOPIC = "hs2026/telemetry"
        private const val CLIENT_ID_PREFIX = "AndroidDiagTool-"
    }

    private var mqttClient: MqttClient? = null
    private val gson = Gson()

    fun connect() {
        if (mqttClient?.isConnected == true) {
            Log.d(TAG, "Already connected")
            return
        }

        try {
            if (mqttClient == null) {
                val clientId = CLIENT_ID_PREFIX + (System.currentTimeMillis() % 10000)
                mqttClient = MqttClient(BROKER_URI, clientId, MemoryPersistence())

                mqttClient?.setCallback(object : MqttCallbackExtended {
                    override fun connectComplete(reconnect: Boolean, serverURI: String?) {
                        Log.i(TAG, "Connected to HiveMQ (reconnect: $reconnect)")
                        try {
                            mqttClient?.subscribe(TOPIC, 0)
                            onConnectionChanged(true)
                        } catch (e: MqttException) {
                            Log.e(TAG, "Subscribe failed: ${e.message}")
                        }
                    }

                    override fun connectionLost(cause: Throwable?) {
                        Log.w(TAG, "Connection lost: ${cause?.message}")
                        onConnectionChanged(false)
                    }

                    override fun messageArrived(topic: String?, message: MqttMessage?) {
                        val payload = message?.toString() ?: return
                        Log.d(TAG, "Message received on $topic: $payload")
                        try {
                            val data = gson.fromJson(payload, EspData::class.java)
                            onDataReceived(data)
                        } catch (e: Exception) {
                            Log.e(TAG, "Failed to parse JSON: ${e.message}")
                        }
                    }

                    override fun deliveryComplete(token: IMqttDeliveryToken?) {}
                })
            }

            val options = MqttConnectOptions().apply {
                userName = USERNAME
                password = PASSWORD.toCharArray()
                isCleanSession = true
                connectionTimeout = 15
                keepAliveInterval = 30
                isAutomaticReconnect = true
                
                // Trust all certs — matches ESP32's espClient.setInsecure()
                val trustAll = arrayOf<TrustManager>(object : X509TrustManager {
                    override fun checkClientTrusted(chain: Array<X509Certificate>?, authType: String?) {}
                    override fun checkServerTrusted(chain: Array<X509Certificate>?, authType: String?) {}
                    override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
                })
                val sslContext = SSLContext.getInstance("TLS")
                sslContext.init(null, trustAll, SecureRandom())
                socketFactory = sslContext.socketFactory
            }

            Log.d(TAG, "Connecting to MQTT broker...")
            mqttClient?.connect(options)

        } catch (e: MqttException) {
            Log.e(TAG, "MQTT connect failed: ${e.reasonCode} - ${e.message}")
            onConnectionChanged(false)
        }
    }

    fun disconnect() {
        try {
            if (mqttClient?.isConnected == true) {
                mqttClient?.disconnect()
            }
            mqttClient?.close()
            mqttClient = null
        } catch (e: MqttException) {
            Log.e(TAG, "MQTT disconnect error: ${e.message}")
        }
    }

    fun isConnected(): Boolean = mqttClient?.isConnected == true
}

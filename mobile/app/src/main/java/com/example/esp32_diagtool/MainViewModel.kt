package com.example.esp32_diagtool

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.example.esp32_diagtool.model.EspData
import com.example.esp32_diagtool.model.GpioPacket
import com.example.esp32_diagtool.model.ImuStreamPacket

data class EspDataPoint(val data: EspData, val receivedAt: Long)

class MainViewModel : ViewModel() {
    companion object {
        private const val TAG = "MainViewModel"
    }

    private val _espData = MutableLiveData<EspData>()
    val espData: LiveData<EspData> = _espData

    private val _imuData = MutableLiveData<ImuStreamPacket>()
    val imuData: LiveData<ImuStreamPacket> = _imuData

    private val _gpioStates = MutableLiveData(defaultGpioStateMap())
    val gpioStates: LiveData<Map<String, Boolean>> = _gpioStates

    private val _logHistory = MutableLiveData<List<EspDataPoint>>(emptyList())
    val logHistory: LiveData<List<EspDataPoint>> = _logHistory

    private val historyList = mutableListOf<EspDataPoint>()
    private var espUpdateCount = 0L
    private var imuUpdateCount = 0L
    private var gpioUpdateCount = 0L

    var startTime: Long = -1L

    fun updateData(data: EspData) {
        val now = System.currentTimeMillis()
        synchronized(historyList) {
            if (startTime == -1L) {
                startTime = now
            }
            val point = EspDataPoint(data, now)
            historyList.add(point)
            _espData.postValue(data)
            _logHistory.postValue(historyList.toList())
            espUpdateCount++
            if (espUpdateCount <= 3L || espUpdateCount % 50L == 0L) {
                Log.d(
                    TAG,
                    "ESP update #$espUpdateCount temp=${data.temperature} volt=${data.voltage} curr=${data.currentNow} battery=${data.batteryPercentage} history=${historyList.size} thread=${Thread.currentThread().name}"
                )
            }
        }
    }

    fun updateImuData(packet: ImuStreamPacket) {
        _imuData.postValue(packet)
        imuUpdateCount++
        if (imuUpdateCount <= 3L || imuUpdateCount % 100L == 0L) {
            Log.d(
                TAG,
                "IMU update #$imuUpdateCount seq=${packet.sequence} sampleUs=${packet.sampleMicros} thread=${Thread.currentThread().name}"
            )
        }
    }

    fun updateGpioStates(packet: GpioPacket) {
        val map = mapOf(
            "GPIO4" to packet.gpio4.isHigh(),
            "GPIO3" to packet.gpio3.isHigh(),
            "GPIO2" to packet.gpio2.isHigh(),
            "GPIO1" to packet.gpio1.isHigh(),
            "GPIO0" to packet.gpio0.isHigh(),
            "GPIO21" to packet.gpio21.isHigh(),
            "GPIO20" to packet.gpio20.isHigh(),
            "GPIO10" to packet.gpio10.isHigh(),
            "GPIO9" to packet.gpio9.isHigh(),
            "GPIO8" to packet.gpio8.isHigh(),
            "GPIO7" to packet.gpio7.isHigh(),
            "GPIO6" to packet.gpio6.isHigh(),
            "GPIO5" to packet.gpio5.isHigh()
        )
        _gpioStates.postValue(map)
        gpioUpdateCount++
        if (gpioUpdateCount <= 3L || gpioUpdateCount % 50L == 0L) {
            Log.d(
                TAG,
                "GPIO update #$gpioUpdateCount $map thread=${Thread.currentThread().name}"
            )
        }
    }

    private fun Int.isHigh(): Boolean = this != 0

    private fun defaultGpioStateMap(): Map<String, Boolean> {
        return mapOf(
            "GPIO4" to false,
            "GPIO3" to false,
            "GPIO2" to false,
            "GPIO1" to false,
            "GPIO0" to false,
            "GPIO21" to false,
            "GPIO20" to false,
            "GPIO10" to false,
            "GPIO9" to false,
            "GPIO8" to false,
            "GPIO7" to false,
            "GPIO6" to false,
            "GPIO5" to false
        )
    }
}

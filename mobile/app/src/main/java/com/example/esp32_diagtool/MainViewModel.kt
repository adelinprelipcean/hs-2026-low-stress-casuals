package com.example.esp32_diagtool

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.example.esp32_diagtool.model.EspData
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

    private val _logHistory = MutableLiveData<List<EspDataPoint>>(emptyList())
    val logHistory: LiveData<List<EspDataPoint>> = _logHistory

    private val historyList = mutableListOf<EspDataPoint>()
    private var espUpdateCount = 0L
    private var imuUpdateCount = 0L

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
}

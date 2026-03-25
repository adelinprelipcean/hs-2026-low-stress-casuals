package com.example.esp32_diagtool

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.example.esp32_diagtool.model.EspData

data class EspDataPoint(val data: EspData, val receivedAt: Long)

class MainViewModel : ViewModel() {
    private val _espData = MutableLiveData<EspData>()
    val espData: LiveData<EspData> = _espData

    private val _logHistory = MutableLiveData<List<EspDataPoint>>(emptyList())
    val logHistory: LiveData<List<EspDataPoint>> = _logHistory

    private val historyList = mutableListOf<EspDataPoint>()

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
        }
    }
}

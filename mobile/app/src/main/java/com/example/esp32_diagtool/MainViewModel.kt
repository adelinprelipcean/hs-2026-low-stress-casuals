package com.example.esp32_diagtool

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.example.esp32_diagtool.model.EspData

class MainViewModel : ViewModel() {
    private val _espData = MutableLiveData<EspData>()
    val espData: LiveData<EspData> = _espData

    private val _logHistory = MutableLiveData<List<EspData>>(emptyList())
    val logHistory: LiveData<List<EspData>> = _logHistory

    fun updateData(data: EspData) {
        _espData.postValue(data)
        val currentHistory = _logHistory.value ?: emptyList()
        _logHistory.postValue(currentHistory + data)
    }
}

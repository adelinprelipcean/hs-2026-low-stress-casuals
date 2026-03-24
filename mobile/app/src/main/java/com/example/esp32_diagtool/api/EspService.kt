package com.example.esp32_diagtool.api

import com.example.esp32_diagtool.model.EspData
import retrofit2.http.GET

interface EspService {
    @GET("get_info")
    suspend fun getInfo(): EspData
}

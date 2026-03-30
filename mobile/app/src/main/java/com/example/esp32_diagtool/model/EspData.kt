package com.example.esp32_diagtool.model

import com.google.gson.annotations.SerializedName

data class EspData(
    @SerializedName("temperature") val temperature: Float,
    @SerializedName("light_intensity") val lightIntensity: Float,
    @SerializedName("io_log") val ioLog: String,
    @SerializedName("timestamp") val timestamp: String,
    @SerializedName("rssi") val rssi: Int,
    @SerializedName("network_name") val networkName: String,
    @SerializedName("cpu_load") val cpuLoad: Float,
    @SerializedName("voltage") val voltage: Float,
    @SerializedName("current_now") val currentNow: Float,
    @SerializedName("current_total") val currentTotal: Float,
    @SerializedName("battery_life") val batteryLife: String,
    @SerializedName("gpio_pin") val gpioPin: String,
    @SerializedName("battery_percentage") val batteryPercentage: Float
)

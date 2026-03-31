package com.example.esp32_diagtool.model

data class TelemetryPacket(
    val header: Short,
    val sampleMs: Long,
    val temp: Float,
    val volt: Float,
    val curr: Float,
    val bat: Int,
    val cpu: Int,
    val rtcHour: Int,
    val rtcMin: Int,
    val rtcSec: Int
)
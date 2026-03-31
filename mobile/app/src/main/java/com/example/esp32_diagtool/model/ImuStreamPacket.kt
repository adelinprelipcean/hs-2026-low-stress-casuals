package com.example.esp32_diagtool.model

data class ImuStreamPacket(
    val header: Short,
    val sequence: Long,
    val sampleMicros: Long,
    val gyroX: Short,
    val gyroY: Short,
    val gyroZ: Short,
    val accelX: Short,
    val accelY: Short,
    val accelZ: Short
)

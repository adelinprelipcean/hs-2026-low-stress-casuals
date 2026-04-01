package com.example.esp32_diagtool.model

data class GpioPacket(
    val header: Short,
    val gpio4: Int,
    val gpio3: Int,
    val gpio2: Int,
    val gpio1: Int,
    val gpio0: Int,
    val gpio21: Int,
    val gpio20: Int,
    val gpio10: Int,
    val gpio9: Int,
    val gpio8: Int,
    val gpio7: Int,
    val gpio6: Int,
    val gpio5: Int
)

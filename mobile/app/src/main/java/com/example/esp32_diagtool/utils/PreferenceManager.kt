package com.example.esp32_diagtool.utils

import android.content.Context
import android.content.SharedPreferences

class PreferenceManager(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("esp32_prefs", Context.MODE_PRIVATE)

    var isFahrenheit: Boolean
        get() = prefs.getBoolean("is_fahrenheit", false)
        set(value) = prefs.edit().putBoolean("is_fahrenheit", value).apply()
}

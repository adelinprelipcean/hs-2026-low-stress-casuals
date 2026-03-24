package com.example.esp32_diagtool.utils

import android.content.Context
import android.content.SharedPreferences

class PreferenceManager(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("esp32_prefs", Context.MODE_PRIVATE)

    var serverUrl: String
        get() = prefs.getString("server_url", "http://192.168.1.100/") ?: "http://192.168.1.100/"
        set(value) = prefs.edit().putString("server_url", if (value.endsWith("/")) value else "$value/").apply()

    var isFahrenheit: Boolean
        get() = prefs.getBoolean("is_fahrenheit", false)
        set(value) = prefs.edit().putBoolean("is_fahrenheit", value).apply()
}

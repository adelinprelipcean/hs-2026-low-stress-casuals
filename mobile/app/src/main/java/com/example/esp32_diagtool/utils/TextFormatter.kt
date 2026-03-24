package com.example.esp32_diagtool.utils

import android.text.Spannable
import android.text.SpannableString
import android.text.style.RelativeSizeSpan
import java.util.Locale

object TextFormatter {
    fun formatFloat(value: Double, decimals: Int, unit: String): CharSequence {
        val fullString = String.format(Locale.getDefault(), "%.${decimals}f %s", value, unit)
        val dotIndex = fullString.indexOf('.')
        if (dotIndex == -1) return fullString

        val spannable = SpannableString(fullString)
        // Make the decimal part and unit smaller (0.7 of original size)
        spannable.setSpan(
            RelativeSizeSpan(0.7f),
            dotIndex,
            fullString.length,
            Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        return spannable
    }

    fun formatFloat(value: Float, decimals: Int, unit: String): CharSequence {
        return formatFloat(value.toDouble(), decimals, unit)
    }
}

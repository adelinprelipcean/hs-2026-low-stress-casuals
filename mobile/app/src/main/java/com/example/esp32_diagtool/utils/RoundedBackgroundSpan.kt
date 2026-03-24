package com.example.esp32_diagtool.utils

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.text.style.ReplacementSpan

/**
 * Custom span to draw a rounded background "flare" around text.
 */
class RoundedBackgroundSpan(
    private val backgroundColor: Int,
    private val textColor: Int,
    private val cornerRadius: Float,
    private val padding: Float
) : ReplacementSpan() {

    override fun getSize(paint: Paint, text: CharSequence?, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
        return (paint.measureText(text, start, end) + 2 * padding).toInt()
    }

    override fun draw(canvas: Canvas, text: CharSequence?, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
        val textWidth = paint.measureText(text, start, end)
        // Define the rectangle for the background, slightly inset vertically
        val rect = RectF(x, top.toFloat() + 4f, x + textWidth + 2 * padding, bottom.toFloat() - 4f)
        
        val originalColor = paint.color
        
        // Draw background
        paint.color = backgroundColor
        canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)
        
        // Draw text
        paint.color = textColor
        canvas.drawText(text!!, start, end, x + padding, y.toFloat(), paint)
        
        paint.color = originalColor
    }
}

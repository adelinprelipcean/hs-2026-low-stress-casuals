package com.example.esp32_diagtool.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat
import com.example.esp32_diagtool.R
import kotlin.math.max

class PinHistoryGraphView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val history = ArrayList<Boolean>()

    private val lowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = ContextCompat.getColor(context, R.color.pin_low_gray)
    }

    private val highPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = ContextCompat.getColor(context, R.color.pin_high_red)
    }

    private val dividerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 0.1f
        color = Color.argb(20, 255, 255, 255)
    }

    fun setHistory(samples: List<Boolean>) {
        history.clear()
        history.addAll(samples)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (history.isEmpty() || width <= 0 || height <= 0) {
            return
        }

        val barCount = history.size
        val barWidth = width.toFloat() / barCount
        val halfHeight = height * 0.5f

        canvas.drawLine(0f, halfHeight, width.toFloat(), halfHeight, dividerPaint)

        history.forEachIndexed { index, isHigh ->
            val left = index * barWidth
            val right = max(left + 1f, (index + 1) * barWidth)
            if (isHigh) {
                canvas.drawRect(left, 0f, right, halfHeight, highPaint)
            } else {
                canvas.drawRect(left, halfHeight, right, height.toFloat(), lowPaint)
            }
        }
    }
}

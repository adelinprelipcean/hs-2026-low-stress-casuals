package com.example.esp32_diagtool.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.opengl.Matrix
import android.util.AttributeSet
import android.view.View
import kotlin.math.min

class AxisGizmoView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private data class AxisEndpoint(
        val label: String,
        val color: Int,
        val x: Float,
        val y: Float,
        val z: Float
    )

    private val orientationMatrix = FloatArray(16)
    private val tempRotationMatrix = FloatArray(16)

    private val axisPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeWidth = dpToPx(2f)
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = dpToPx(11f)
        style = Paint.Style.FILL
        textAlign = Paint.Align.CENTER
    }

    private val centerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#D9FFFFFF")
        style = Paint.Style.FILL
    }

    init {
        Matrix.setIdentityM(orientationMatrix, 0)
    }

    fun setEulerRotation(rotX: Float, rotY: Float, rotZ: Float) {
        Matrix.setIdentityM(tempRotationMatrix, 0)
        Matrix.rotateM(tempRotationMatrix, 0, -rotY, 1f, 0f, 0f)
        Matrix.rotateM(tempRotationMatrix, 0, rotZ, 0f, 1f, 0f)
        Matrix.rotateM(tempRotationMatrix, 0, -rotX, 0f, 0f, 1f)

        System.arraycopy(tempRotationMatrix, 0, orientationMatrix, 0, orientationMatrix.size)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (width <= 0 || height <= 0) return

        val cx = width * 0.5f
        val cy = height * 0.5f
        val axisLength = min(width, height) * 0.34f
        val centerRadius = dpToPx(3f)

        val endpoints = mutableListOf(
            transformedAxisEndpoint("X", Color.parseColor("#FF5C5C"), 1f, 0f, 0f, cx, cy, axisLength),
            transformedAxisEndpoint("Y", Color.parseColor("#4CC97D"), 0f, 1f, 0f, cx, cy, axisLength),
            transformedAxisEndpoint("Z", Color.parseColor("#4F8CFF"), 0f, 0f, 1f, cx, cy, axisLength)
        )

        endpoints.sortBy { it.z }

        for (axis in endpoints) {
            axisPaint.color = axis.color
            canvas.drawLine(cx, cy, axis.x, axis.y, axisPaint)
            canvas.drawCircle(axis.x, axis.y, dpToPx(2.5f), axisPaint)
            canvas.drawText(axis.label, axis.x, axis.y - dpToPx(6f), textPaint)
        }

        canvas.drawCircle(cx, cy, centerRadius, centerPaint)
    }

    private fun transformedAxisEndpoint(
        label: String,
        color: Int,
        x: Float,
        y: Float,
        z: Float,
        cx: Float,
        cy: Float,
        axisLength: Float
    ): AxisEndpoint {
        val rx = orientationMatrix[0] * x + orientationMatrix[4] * y + orientationMatrix[8] * z
        val ry = orientationMatrix[1] * x + orientationMatrix[5] * y + orientationMatrix[9] * z
        val rz = orientationMatrix[2] * x + orientationMatrix[6] * y + orientationMatrix[10] * z

        val depthScale = 1f + (rz * 0.35f)
        val sx = cx + rx * axisLength * depthScale
        val sy = cy - ry * axisLength * depthScale

        return AxisEndpoint(label = label, color = color, x = sx, y = sy, z = rz)
    }

    private fun dpToPx(dp: Float): Float = dp * resources.displayMetrics.density
}

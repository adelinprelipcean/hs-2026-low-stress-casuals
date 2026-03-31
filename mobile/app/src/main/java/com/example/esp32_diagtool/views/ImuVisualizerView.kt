package com.example.esp32_diagtool.views

import android.content.Context
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.Matrix
import android.util.AttributeSet
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class ImuVisualizerView(context: Context, attrs: AttributeSet?) : GLSurfaceView(context, attrs) {

    private val renderer: ImuRenderer

    init {
        setEGLContextClientVersion(2)
        renderer = ImuRenderer()
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun updateRotation(gx: Short, gy: Short, gz: Short) {
        // Simple integration or mapping for visualization
        // In a real app, you'd use a sensor fusion algorithm (Kalman/Madgwick)
        // Here we just map gyro values to rotation speed or simple angles for demo
        renderer.rotationX += gx / 500f
        renderer.rotationY += gy / 500f
        renderer.rotationZ += gz / 500f
    }

    private class ImuRenderer : Renderer {
        private var cube: Cube? = null
        var rotationX = 0f
        var rotationY = 0f
        var rotationZ = 0f

        private val vPMatrix = FloatArray(16)
        private val projectionMatrix = FloatArray(16)
        private val viewMatrix = FloatArray(16)
        private val rotationMatrix = FloatArray(16)
        private val tempMatrix = FloatArray(16)

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            GLES20.glClearColor(0.1f, 0.1f, 0.1f, 1.0f)
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
            cube = Cube()
        }

        override fun onDrawFrame(gl: GL10?) {
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)

            Matrix.setLookAtM(viewMatrix, 0, 0f, 0f, -5f, 0f, 0f, 0f, 0f, 1.0f, 0.0f)
            Matrix.multiplyMM(vPMatrix, 0, projectionMatrix, 0, viewMatrix, 0)

            val modelMatrix = FloatArray(16)
            Matrix.setIdentityM(modelMatrix, 0)
            Matrix.rotateM(modelMatrix, 0, rotationX, 1f, 0f, 0f)
            Matrix.rotateM(modelMatrix, 0, rotationY, 0f, 1f, 0f)
            Matrix.rotateM(modelMatrix, 0, rotationZ, 0f, 0f, 1f)

            val scratch = FloatArray(16)
            Matrix.multiplyMM(scratch, 0, vPMatrix, 0, modelMatrix, 0)

            cube?.draw(scratch)
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            GLES20.glViewport(0, 0, width, height)
            val ratio: Float = width.toFloat() / height.toFloat()
            Matrix.frustumM(projectionMatrix, 0, -ratio, ratio, -1f, 1f, 3f, 7f)
        }
    }

    private class Cube {
        private val vertexShaderCode =
            "uniform mat4 uVPMatrix;" +
            "attribute vec4 vPosition;" +
            "attribute vec4 vColor;" +
            "varying vec4 _vColor;" +
            "void main() {" +
            "  gl_Position = uVPMatrix * vPosition;" +
            "  _vColor = vColor;" +
            "}"

        private val fragmentShaderCode =
            "precision mediump float;" +
            "varying vec4 _vColor;" +
            "void main() {" +
            "  gl_FragColor = _vColor;" +
            "}"

        private val vertexBuffer: FloatBuffer
        private val colorBuffer: FloatBuffer
        private val drawListBuffer: ByteBuffer
        private var mProgram: Int = 0

        private val cubeCoords = floatArrayOf(
            -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f,  0.5f, -0.5f
        )

        private val colors = floatArrayOf(
            1f, 0f, 0f, 1f,
            0f, 1f, 0f, 1f,
            0f, 0f, 1f, 1f,
            1f, 1f, 0f, 1f,
            0f, 1f, 1f, 1f,
            1f, 0f, 1f, 1f,
            1f, 1f, 1f, 1f,
            0.5f, 0.5f, 0.5f, 1f
        )

        private val drawOrder = byteArrayOf(
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4,
            3, 2, 6, 3, 6, 7,
            0, 3, 7, 0, 7, 4,
            1, 2, 6, 1, 6, 5
        )

        init {
            val bb = ByteBuffer.allocateDirect(cubeCoords.size * 4)
            bb.order(ByteOrder.nativeOrder())
            vertexBuffer = bb.asFloatBuffer()
            vertexBuffer.put(cubeCoords)
            vertexBuffer.position(0)

            val cb = ByteBuffer.allocateDirect(colors.size * 4)
            cb.order(ByteOrder.nativeOrder())
            colorBuffer = cb.asFloatBuffer()
            colorBuffer.put(colors)
            colorBuffer.position(0)

            drawListBuffer = ByteBuffer.allocateDirect(drawOrder.size)
            drawListBuffer.put(drawOrder)
            drawListBuffer.position(0)

            val vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexShaderCode)
            val fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentShaderCode)

            mProgram = GLES20.glCreateProgram().also {
                GLES20.glAttachShader(it, vertexShader)
                GLES20.glAttachShader(it, fragmentShader)
                GLES20.glLinkProgram(it)
            }
        }

        fun draw(mvpMatrix: FloatArray) {
            GLES20.glUseProgram(mProgram)

            val positionHandle = GLES20.glGetAttribLocation(mProgram, "vPosition")
            GLES20.glEnableVertexAttribArray(positionHandle)
            GLES20.glVertexAttribPointer(positionHandle, 3, GLES20.GL_FLOAT, false, 0, vertexBuffer)

            val colorHandle = GLES20.glGetAttribLocation(mProgram, "vColor")
            GLES20.glEnableVertexAttribArray(colorHandle)
            GLES20.glVertexAttribPointer(colorHandle, 4, GLES20.GL_FLOAT, false, 0, colorBuffer)

            val vPMatrixHandle = GLES20.glGetUniformLocation(mProgram, "uVPMatrix")
            GLES20.glUniformMatrix4fv(vPMatrixHandle, 1, false, mvpMatrix, 0)

            GLES20.glDrawElements(GLES20.GL_TRIANGLES, drawOrder.size, GLES20.GL_UNSIGNED_BYTE, drawListBuffer)

            GLES20.glDisableVertexAttribArray(positionHandle)
            GLES20.glDisableVertexAttribArray(colorHandle)
        }

        private fun loadShader(type: Int, shaderCode: String): Int {
            return GLES20.glCreateShader(type).also { shader ->
                GLES20.glShaderSource(shader, shaderCode)
                GLES20.glCompileShader(shader)
            }
        }
    }
}

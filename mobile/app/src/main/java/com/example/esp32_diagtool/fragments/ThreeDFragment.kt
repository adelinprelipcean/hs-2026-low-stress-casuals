package com.example.esp32_diagtool.fragments

import android.opengl.Matrix
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentThreeDBinding
import com.example.esp32_diagtool.model.ImuStreamPacket
import com.google.android.filament.EntityManager
import com.google.android.filament.LightManager
import com.google.android.filament.Skybox
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.utils.ModelViewer
import com.google.android.filament.utils.Utils
import java.nio.ByteBuffer
import java.util.Locale
import kotlin.math.atan2
import kotlin.math.sqrt

class ThreeDFragment : Fragment() {
    private val modelScaleFactor = 1.0f
    private val complementaryAlpha = 0.96f
    private var observedImuCount = 0L

    private var _binding: FragmentThreeDBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    private lateinit var choreographer: Choreographer
    private lateinit var modelViewer: ModelViewer
    private val transformMatrix = FloatArray(16)
    private val pivotedBaseMatrix = FloatArray(16)
    private val composedTransformMatrix = FloatArray(16)
    private val baseTransformMatrix = FloatArray(16)
    private val pivotPreTranslateMatrix = FloatArray(16)
    private val pivotPostTranslateMatrix = FloatArray(16)
    private val pivotRotationMatrix = FloatArray(16)
    private val pivotCenterLocal = FloatArray(4)
    private val pivotCenterWorld = FloatArray(4)
    private var isBaseTransformInitialized = false
    private var isPivotInitialized = false
    private var mainLightEntity: Int = 0
    private var backgroundSkybox: Skybox? = null
    private var isRenderLoopRunning = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentThreeDBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        setupFilament()

        viewModel.imuData.observe(viewLifecycleOwner) { packet ->
            observedImuCount++
            if (observedImuCount <= 3L || observedImuCount % 100L == 0L) {
                Log.d(TAG, "Observed IMU #$observedImuCount seq=${packet.sequence} sampleUs=${packet.sampleMicros}")
            }
            onImuDataReceived(packet)
        }
    }

    private fun setupFilament() {
        choreographer = Choreographer.getInstance()
        modelViewer = ModelViewer(binding.surfaceView)

        backgroundSkybox = Skybox.Builder()
            .color(0.32f, 0.34f, 0.36f, 1.0f)
            .build(modelViewer.engine)
        modelViewer.scene.skybox = backgroundSkybox

        // Load model from assets
        val buffer = readAsset("esp32.glb")
        modelViewer.loadModelGlb(buffer)
        modelViewer.transformToUnitCube()
        Matrix.setIdentityM(baseTransformMatrix, 0)
        Matrix.setIdentityM(pivotPreTranslateMatrix, 0)
        Matrix.setIdentityM(pivotPostTranslateMatrix, 0)
        Matrix.setIdentityM(pivotRotationMatrix, 0)
        isBaseTransformInitialized = false
        isPivotInitialized = false
        binding.axisGizmo.setEulerRotation(0f, 0f, 0f)

        mainLightEntity = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.DIRECTIONAL)
            .color(1.0f, 0.98f, 0.92f)
            .intensity(60_000.0f)
            .direction(0.4f, -1.0f, -0.6f)
            .castShadows(false)
            .build(modelViewer.engine, mainLightEntity)
        modelViewer.scene.addEntity(mainLightEntity)

        modelViewer.asset?.let { asset ->
            val tm = modelViewer.engine.transformManager
            val instance = tm.getInstance(asset.root)
            tm.getTransform(instance, baseTransformMatrix)
            Matrix.scaleM(baseTransformMatrix, 0, modelScaleFactor, modelScaleFactor, modelScaleFactor)
            tm.setTransform(instance, baseTransformMatrix)
            isBaseTransformInitialized = true

            initializePivotMatrices(asset)
        }

        // Lighting & Loop
        modelViewer.view.isPostProcessingEnabled = false
        startRenderLoop()
    }

    private fun startRenderLoop() {
        if (!isRenderLoopRunning) {
            isRenderLoopRunning = true
            choreographer.postFrameCallback(frameCallback)
        }
    }

    private fun stopRenderLoop() {
        if (isRenderLoopRunning) {
            choreographer.removeFrameCallback(frameCallback)
            isRenderLoopRunning = false
        }
    }

    private fun initializePivotMatrices(asset: FilamentAsset) {
        val pivotCenter = asset.boundingBox.center
        pivotCenterLocal[0] = pivotCenter[0]
        pivotCenterLocal[1] = pivotCenter[1]
        pivotCenterLocal[2] = pivotCenter[2]
        pivotCenterLocal[3] = 1f

        Matrix.multiplyMV(pivotCenterWorld, 0, baseTransformMatrix, 0, pivotCenterLocal, 0)
        val pivotX = pivotCenterWorld[0]
        val pivotY = pivotCenterWorld[1]
        val pivotZ = pivotCenterWorld[2]

        Matrix.setIdentityM(pivotPreTranslateMatrix, 0)
        Matrix.translateM(pivotPreTranslateMatrix, 0, pivotX, pivotY, pivotZ)
        Matrix.setIdentityM(pivotPostTranslateMatrix, 0)
        Matrix.translateM(pivotPostTranslateMatrix, 0, -pivotX, -pivotY, -pivotZ)
        isPivotInitialized = true
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!isRenderLoopRunning || _binding == null) {
                return
            }

            choreographer.postFrameCallback(this)
            modelViewer.render(frameTimeNanos)
        }
    }

    private fun readAsset(assetName: String): ByteBuffer {
        val input = requireContext().assets.open(assetName)
        val bytes = input.readBytes()
        return ByteBuffer.allocateDirect(bytes.size).apply {
            put(bytes)
            flip()
        }
    }

    private var currentRotX = 0f
    private var currentRotY = 0f
    private var currentRotZ = 0f
    private var lastSampleMicros: Long? = null

    private fun normalizeAngleDeg(angle: Float): Float {
        var normalized = angle % 360f
        if (normalized > 180f) normalized -= 360f
        if (normalized < -180f) normalized += 360f
        return normalized
    }

    private fun onImuDataReceived(packet: ImuStreamPacket) {
        // Integrate gyro first, then use accel tilt as a slow correction to reduce drift.
        currentRotX = normalizeAngleDeg(currentRotX + packet.gyroX / 500f)
        currentRotY = normalizeAngleDeg(currentRotY + packet.gyroY / 500f)
        currentRotZ = normalizeAngleDeg(currentRotZ + packet.gyroZ / 500f)

        val accelX = packet.accelX.toFloat()
        val accelY = packet.accelY.toFloat()
        val accelZ = packet.accelZ.toFloat()
        val accelMagnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ)

        if (accelMagnitude > 1e-3f) {
            val accelAngleX = Math.toDegrees(atan2(accelY.toDouble(), accelZ.toDouble())).toFloat()
            val accelAngleY = Math.toDegrees(
                atan2(
                    (-accelX).toDouble(),
                    sqrt((accelY * accelY + accelZ * accelZ).toDouble())
                )
            ).toFloat()

            val hasSampleDelta = lastSampleMicros?.let { packet.sampleMicros > it } ?: false
            if (hasSampleDelta) {
                currentRotX = normalizeAngleDeg(
                    complementaryAlpha * currentRotX + (1f - complementaryAlpha) * accelAngleX
                )
                currentRotY = normalizeAngleDeg(
                    complementaryAlpha * currentRotY + (1f - complementaryAlpha) * accelAngleY
                )
            }
        }

        lastSampleMicros = packet.sampleMicros
        binding.axisGizmo.setEulerRotation(currentRotX, currentRotY, currentRotZ)

        modelViewer.asset?.let { asset ->
            val tm = modelViewer.engine.transformManager
            val entity = asset.root
            val instance = tm.getInstance(entity)

            if (!isBaseTransformInitialized) {
                tm.getTransform(instance, baseTransformMatrix)
                Matrix.scaleM(baseTransformMatrix, 0, modelScaleFactor, modelScaleFactor, modelScaleFactor)
                tm.setTransform(instance, baseTransformMatrix)
                isBaseTransformInitialized = true
            }
            if (!isPivotInitialized) {
                initializePivotMatrices(asset)
            }
            
            Matrix.setIdentityM(transformMatrix, 0)
            Matrix.rotateM(transformMatrix, 0, -currentRotY, 1f, 0f, 0f)
            Matrix.rotateM(transformMatrix, 0, currentRotZ, 0f, 1f, 0f)
            Matrix.rotateM(transformMatrix, 0, -currentRotX, 0f, 0f, 1f)

            // Rotate around model center in world space: T(center) * R * T(-center) * Base
            Matrix.multiplyMM(pivotedBaseMatrix, 0, pivotPostTranslateMatrix, 0, baseTransformMatrix, 0)
            Matrix.multiplyMM(pivotRotationMatrix, 0, transformMatrix, 0, pivotedBaseMatrix, 0)
            Matrix.multiplyMM(composedTransformMatrix, 0, pivotPreTranslateMatrix, 0, pivotRotationMatrix, 0)
            
            tm.setTransform(instance, composedTransformMatrix)
        }

        binding.tvImuStatus.text = String.format(
            Locale.getDefault(),
            "Gyro: %d, %d, %d | Accel: %d, %d, %d",
            packet.gyroX, packet.gyroY, packet.gyroZ,
            packet.accelX, packet.accelY, packet.accelZ
        )
        binding.tvSequence.text = "Seq: ${packet.sequence}"
    }

    override fun onResume() {
        super.onResume()
        startRenderLoop()
    }

    override fun onPause() {
        super.onPause()
        stopRenderLoop()
    }

    override fun onDestroyView() {
        stopRenderLoop()
        backgroundSkybox?.let {
            modelViewer.scene.skybox = null
            modelViewer.engine.destroySkybox(it)
            backgroundSkybox = null
        }
        if (mainLightEntity != 0) {
            modelViewer.scene.removeEntity(mainLightEntity)
            modelViewer.engine.destroyEntity(mainLightEntity)
            EntityManager.get().destroy(mainLightEntity)
            mainLightEntity = 0
        }
        super.onDestroyView()
        _binding = null
    }

    companion object {
        init {
            Utils.init()
        }
        private const val TAG = "ThreeDFragment"
    }
}

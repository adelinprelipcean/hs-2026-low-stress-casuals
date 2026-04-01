package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.util.Log
import android.view.MotionEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewClientCompat
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentThreeDBinding
import com.example.esp32_diagtool.model.ImuStreamPacket
import java.util.Locale
import kotlin.math.atan2
import kotlin.math.max
import kotlin.math.sqrt

class ThreeDFragment : Fragment() {
    private val gyroLsbPerDps = 131f
    private val accelCorrectionTimeConstantSec = 0.55f
    private val defaultSampleDeltaSec = 0.01f
    private val gyroBiasLearnRate = 0.02f
    private val stationaryGyroThresholdDps = 1.5f
    private val accelReliabilityTolerance = 0.12f

    private var observedImuCount = 0L

    private var _binding: FragmentThreeDBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    private var isWebViewerReady = false
    private var pendingRotX = 0f
    private var pendingRotY = 0f
    private var pendingRotZ = 0f
    private lateinit var assetLoader: WebViewAssetLoader

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

        setupWebView(binding.webView3d)
        binding.btnRecenter.setOnClickListener { recenterModel() }

        viewModel.imuData.observe(viewLifecycleOwner) { packet ->
            observedImuCount++
            if (observedImuCount <= 3L || observedImuCount % 100L == 0L) {
                Log.d(TAG, "Observed IMU #$observedImuCount seq=${packet.sequence} sampleUs=${packet.sampleMicros}")
            }
            onImuDataReceived(packet)
        }
    }

    private fun setupWebView(webView: WebView) {
        Log.e(TAG, "Three.js WebView setup started")

        webView.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_MOVE -> view.parent?.requestDisallowInterceptTouchEvent(true)
                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_CANCEL -> view.parent?.requestDisallowInterceptTouchEvent(false)
            }
            false
        }

        assetLoader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(requireContext()))
            .build()

        with(webView.settings) {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = true
            allowContentAccess = true
            mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
            builtInZoomControls = false
            displayZoomControls = false
        }
        webView.setBackgroundColor(0x00000000)
        webView.isVerticalScrollBarEnabled = false
        webView.isHorizontalScrollBarEnabled = false
        webView.webChromeClient = object : WebChromeClient() {
            override fun onConsoleMessage(consoleMessage: ConsoleMessage): Boolean {
                Log.e(
                    TAG,
                    "WV ${consoleMessage.messageLevel()}: ${consoleMessage.message()} @${consoleMessage.lineNumber()}"
                )
                return true
            }
        }
        webView.webViewClient = object : WebViewClientCompat() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: WebResourceRequest
            ): WebResourceResponse? {
                return assetLoader.shouldInterceptRequest(request.url)
            }

            override fun onPageFinished(view: WebView?, url: String?) {
                super.onPageFinished(view, url)
                isWebViewerReady = true
                Log.e(TAG, "Three.js viewer ready: $url")
                pushRotationToWebView(pendingRotX, pendingRotY, pendingRotZ)
            }
        }
        webView.loadUrl("https://appassets.androidplatform.net/assets/three_d_viewer.html")
        binding.axisGizmo.setEulerRotation(0f, 0f, 0f)
    }

    private var currentRotX = 0f
    private var currentRotY = 0f
    private var currentRotZ = 0f
    private var lastSampleMicros: Long? = null
    private var accelNormBaseline: Float? = null
    private var gyroBiasXDps = 0f
    private var gyroBiasYDps = 0f
    private var gyroBiasZDps = 0f
    private var recenterOffsetX = 0f
    private var recenterOffsetY = 0f
    private var recenterOffsetZ = 0f

    private fun normalizeAngleDeg(angle: Float): Float {
        var normalized = angle % 360f
        if (normalized > 180f) normalized -= 360f
        if (normalized < -180f) normalized += 360f
        return normalized
    }

    private fun computeDeltaSec(sampleMicros: Long): Float {
        val previous = lastSampleMicros
        if (previous == null) {
            lastSampleMicros = sampleMicros
            return defaultSampleDeltaSec
        }

        var deltaMicros = sampleMicros - previous
        if (deltaMicros <= 0L) {
            val wrappedDelta = sampleMicros + (1L shl 32) - previous
            deltaMicros = if (wrappedDelta > 0L) wrappedDelta else 0L
        }

        lastSampleMicros = sampleMicros

        if (deltaMicros <= 0L) return defaultSampleDeltaSec
        val clampedMicros = deltaMicros.coerceIn(1L, 250_000L)
        return clampedMicros.toFloat() / 1_000_000f
    }

    private fun recenterModel() {
        recenterOffsetX = currentRotX
        recenterOffsetY = currentRotY
        recenterOffsetZ = currentRotZ

        pendingRotX = 0f
        pendingRotY = 0f
        pendingRotZ = 0f
        pushRotationToWebView(0f, 0f, 0f)
        binding.axisGizmo.setEulerRotation(0f, 0f, 0f)
    }

    private fun onImuDataReceived(packet: ImuStreamPacket) {
        val dtSec = computeDeltaSec(packet.sampleMicros)

        val rawGyroXDps = packet.gyroX / gyroLsbPerDps
        val rawGyroYDps = packet.gyroY / gyroLsbPerDps
        val rawGyroZDps = packet.gyroZ / gyroLsbPerDps

        val accelX = packet.accelX.toFloat()
        val accelY = packet.accelY.toFloat()
        val accelZ = packet.accelZ.toFloat()
        val accelMagnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ)
        val baseline = accelNormBaseline
        val updatedBaseline = when {
            baseline == null -> accelMagnitude
            else -> baseline * 0.995f + accelMagnitude * 0.005f
        }
        accelNormBaseline = updatedBaseline
        val accelMagnitudeRatio = if (updatedBaseline > 1e-3f) {
            accelMagnitude / updatedBaseline
        } else {
            1f
        }
        val accelReliable = kotlin.math.abs(accelMagnitudeRatio - 1f) <= accelReliabilityTolerance

        val gyroMagnitudeDps = sqrt(
            rawGyroXDps * rawGyroXDps +
                rawGyroYDps * rawGyroYDps +
                rawGyroZDps * rawGyroZDps
        )
        if (accelReliable && gyroMagnitudeDps < stationaryGyroThresholdDps) {
            gyroBiasXDps += gyroBiasLearnRate * (rawGyroXDps - gyroBiasXDps)
            gyroBiasYDps += gyroBiasLearnRate * (rawGyroYDps - gyroBiasYDps)
            gyroBiasZDps += gyroBiasLearnRate * (rawGyroZDps - gyroBiasZDps)
        }

        // Integrate gyro rate with sample timing; this avoids drift from variable packet cadence.
        currentRotX = normalizeAngleDeg(currentRotX + (rawGyroXDps - gyroBiasXDps) * dtSec)
        currentRotY = normalizeAngleDeg(currentRotY + (rawGyroYDps - gyroBiasYDps) * dtSec)
        currentRotZ = normalizeAngleDeg(currentRotZ + (rawGyroZDps - gyroBiasZDps) * dtSec)

        if (accelMagnitude > 1e-3f && accelReliable) {
            val accelAngleX = Math.toDegrees(atan2(accelY.toDouble(), accelZ.toDouble())).toFloat()
            val accelAngleY = Math.toDegrees(
                atan2(
                    (-accelX).toDouble(),
                    sqrt((accelY * accelY + accelZ * accelZ).toDouble())
                )
            ).toFloat()

            val alpha = accelCorrectionTimeConstantSec / (accelCorrectionTimeConstantSec + max(dtSec, 1e-4f))
            currentRotX = normalizeAngleDeg(alpha * currentRotX + (1f - alpha) * accelAngleX)
            currentRotY = normalizeAngleDeg(alpha * currentRotY + (1f - alpha) * accelAngleY)
        }

        val viewRotX = normalizeAngleDeg(currentRotX - recenterOffsetX)
        val viewRotY = normalizeAngleDeg(currentRotY - recenterOffsetY)
        val viewRotZ = normalizeAngleDeg(currentRotZ - recenterOffsetZ)

        pendingRotX = viewRotX
        pendingRotY = viewRotY
        pendingRotZ = viewRotZ
        pushRotationToWebView(viewRotX, viewRotY, viewRotZ)
        binding.axisGizmo.setEulerRotation(viewRotX, viewRotY, viewRotZ)

        binding.tvImuStatus.text = String.format(
            Locale.getDefault(),
            "Gyro: %d, %d, %d | Accel: %d, %d, %d",
            packet.gyroX, packet.gyroY, packet.gyroZ,
            packet.accelX, packet.accelY, packet.accelZ
        )
        binding.tvSequence.text = "Seq: ${packet.sequence}"
    }

    private fun pushRotationToWebView(rotX: Float, rotY: Float, rotZ: Float) {
        if (!isWebViewerReady || _binding == null) return
        val js = "window.updateImuRotation(${rotX.toJsNumber()}, ${rotY.toJsNumber()}, ${rotZ.toJsNumber()});"
        binding.webView3d.evaluateJavascript(js, null)
    }

    private fun Float.toJsNumber(): String = String.format(Locale.US, "%.5f", this)

    override fun onResume() {
        super.onResume()
        _binding?.webView3d?.onResume()
    }

    override fun onPause() {
        super.onPause()
        _binding?.webView3d?.onPause()
    }

    override fun onDestroyView() {
        binding.webView3d.apply {
            stopLoading()
            loadUrl("about:blank")
            webViewClient = WebViewClient()
            destroy()
        }
        isWebViewerReady = false
        super.onDestroyView()
        _binding = null
    }

    companion object {
        private const val TAG = "ThreeDFragment"
    }
}

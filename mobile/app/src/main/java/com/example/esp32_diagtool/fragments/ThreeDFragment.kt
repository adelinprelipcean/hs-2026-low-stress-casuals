package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.util.Log
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
import kotlin.math.sqrt

class ThreeDFragment : Fragment() {
    private val complementaryAlpha = 0.96f
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
        pendingRotX = currentRotX
        pendingRotY = currentRotY
        pendingRotZ = currentRotZ
        pushRotationToWebView(currentRotY, currentRotZ, currentRotX)
        binding.axisGizmo.setEulerRotation(currentRotY, currentRotZ, currentRotX)

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

package com.example.esp32_diagtool

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Menu
import android.view.MenuItem
import android.widget.Toast
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.viewpager2.adapter.FragmentStateAdapter
import com.example.esp32_diagtool.databinding.ActivityMainBinding
import com.example.esp32_diagtool.fragments.IoFragment
import com.example.esp32_diagtool.fragments.MainDashboardFragment
import com.example.esp32_diagtool.fragments.ThreeDFragment
import com.example.esp32_diagtool.model.EspData
import com.example.esp32_diagtool.model.GpioPacket
import com.example.esp32_diagtool.model.ImuStreamPacket
import com.example.esp32_diagtool.model.TelemetryPacket
import com.example.esp32_diagtool.network.SocketManager
import java.util.Calendar
import com.google.android.material.tabs.TabLayoutMediator
import java.util.Locale
import kotlin.math.PI
import kotlin.math.roundToInt
import kotlin.math.sin

class MainActivity : AppCompatActivity() {
    companion object {
        private const val TAG = "MainActivity"
        private const val PREFS_NAME = "app_debug_prefs"
        private const val PREF_USE_MOCK_DATA = "use_mock_data"
        private const val MENU_ID_USE_MOCK_DATA = 1001
        private const val MOCK_UPDATE_INTERVAL_MS = 250L
    }

    private lateinit var binding: ActivityMainBinding
    private val viewModel: MainViewModel by viewModels()
    private lateinit var socketManager: SocketManager
    private var imuCallbackCount = 0L
    private var telemetryCallbackCount = 0L
    private var gpioCallbackCount = 0L
    private val mainHandler = Handler(Looper.getMainLooper())
    private var mockRunnable: Runnable? = null
    private var mockSequence = 0L
    private var mockStartMs = 0L
    private var useMockData = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)
        supportActionBar?.title = getString(R.string.app_name)

        setupViewPager()
        initSocket()
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        useMockData = prefs.getBoolean(PREF_USE_MOCK_DATA, false)
        applyDataSourceMode(useMockData, showToast = false)
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        val item = menu.add(Menu.NONE, MENU_ID_USE_MOCK_DATA, Menu.NONE, getString(R.string.use_mock_data))
        item.isCheckable = true
        item.isChecked = useMockData
        item.setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == MENU_ID_USE_MOCK_DATA) {
            val newState = !item.isChecked
            applyDataSourceMode(newState, showToast = true)
            invalidateOptionsMenu()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    private fun setupViewPager() {
        val adapter = object : FragmentStateAdapter(this) {
            override fun getItemCount(): Int = 3
            
            override fun createFragment(position: Int): Fragment {
                return when (position) {
                    0 -> ThreeDFragment()
                    1 -> MainDashboardFragment()
                    else -> IoFragment()
                }
            }
        }
        binding.viewPager.adapter = adapter
        binding.viewPager.offscreenPageLimit = 3
        binding.viewPager.isUserInputEnabled = true

        TabLayoutMediator(binding.tabLayout, binding.viewPager) { tab, position ->
            tab.text = when (position) {
                0 -> getString(R.string.tab_3d_model)
                1 -> getString(R.string.tab_esp_data)
                else -> getString(R.string.tab_io)
            }
        }.attach()
    }

    private fun initSocket() {
        socketManager = SocketManager(
            onImuDataReceived = { packet ->
                imuCallbackCount++
                if (imuCallbackCount <= 3L || imuCallbackCount % 100L == 0L) {
                    Log.d(TAG, "IMU callback #$imuCallbackCount seq=${packet.sequence} sampleUs=${packet.sampleMicros}")
                }
                viewModel.updateImuData(packet)
            },
            onTelemetryDataReceived = { packet ->
                telemetryCallbackCount++
                if (telemetryCallbackCount <= 3L || telemetryCallbackCount % 50L == 0L) {
                    Log.d(
                        TAG,
                        "TEL callback #$telemetryCallbackCount ms=${packet.sampleMs} temp=${packet.temp} volt=${packet.volt} curr=${packet.curr}"
                    )
                }
                viewModel.updateData(packet.toEspData())
            },
            onGpioDataReceived = { packet ->
                gpioCallbackCount++
                if (gpioCallbackCount <= 3L || gpioCallbackCount % 50L == 0L) {
                    Log.d(
                        TAG,
                        "GPIO callback #$gpioCallbackCount ${packet.toLogSummary()}"
                    )
                }
                viewModel.updateGpioStates(packet)
            },
            onConnectionChanged = { connected ->
                Log.d(TAG, "Socket connection status: $connected")
            }
        )
        socketManager.connect()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopMockStream()
        socketManager.disconnect()
    }

    private fun applyDataSourceMode(enableMock: Boolean, showToast: Boolean) {
        useMockData = enableMock
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putBoolean(PREF_USE_MOCK_DATA, enableMock)
            .apply()

        if (enableMock) {
            socketManager.disconnect()
            startMockStream()
            if (showToast) {
                Toast.makeText(this, getString(R.string.mode_mock_enabled), Toast.LENGTH_SHORT).show()
            }
        } else {
            stopMockStream()
            socketManager.connect()
            if (showToast) {
                Toast.makeText(this, getString(R.string.mode_socket_enabled), Toast.LENGTH_SHORT).show()
            }
        }
        Log.d(TAG, "Data source switched: mock=$enableMock")
    }

    private fun startMockStream() {
        if (mockRunnable != null) return

        mockSequence = 0L
        mockStartMs = System.currentTimeMillis()
        val task = object : Runnable {
            override fun run() {
                if (!useMockData) return

                mockSequence++
                val now = System.currentTimeMillis()
                val t = mockSequence.toDouble()

                val imuPacket = ImuStreamPacket(
                    header = 0xA1.toShort(),
                    sequence = mockSequence,
                    sampleMicros = (now - mockStartMs) * 1_000L,
                    gyroX = waveShort(t, 1200.0, 0.0),
                    gyroY = waveShort(t, 900.0, PI / 3.0),
                    gyroZ = waveShort(t, 700.0, PI / 1.5),
                    accelX = waveShort(t, 1500.0, PI / 4.0),
                    accelY = waveShort(t, 1700.0, PI / 2.0),
                    accelZ = waveShort(t, 2100.0, PI / 6.0)
                )
                viewModel.updateImuData(imuPacket)

                val calendar = Calendar.getInstance()
                val telemetryPacket = TelemetryPacket(
                    header = 0xD4.toShort(),
                    sampleMs = now - mockStartMs,
                    temp = 28f + (sin(t * 0.05) * 2.5).toFloat(),
                    volt = 3.55f + (sin(t * 0.03) * 0.12).toFloat(),
                    curr = 0.25f + (sin(t * 0.07 + 0.9) * 0.08).toFloat(),
                    bat = (82 + (sin(t * 0.01) * 12)).roundToInt().coerceIn(10, 100),
                    cpu = (45 + (sin(t * 0.06 + 1.1) * 30)).roundToInt().coerceIn(1, 99),
                    rtcHour = calendar.get(Calendar.HOUR_OF_DAY),
                    rtcMin = calendar.get(Calendar.MINUTE),
                    rtcSec = calendar.get(Calendar.SECOND)
                )
                viewModel.updateData(telemetryPacket.toEspData())

                val gpioPacket = GpioPacket(
                    header = 0xC1.toShort(),
                    gpio4 = squareBit(t, 2),
                    gpio3 = squareBit(t, 3),
                    gpio2 = squareBit(t, 4),
                    gpio1 = squareBit(t, 5),
                    gpio0 = squareBit(t, 6),
                    gpio21 = squareBit(t, 7),
                    gpio20 = squareBit(t, 8),
                    gpio10 = squareBit(t, 9),
                    gpio9 = squareBit(t, 10),
                    gpio8 = squareBit(t, 11),
                    gpio7 = squareBit(t, 12),
                    gpio6 = squareBit(t, 13),
                    gpio5 = squareBit(t, 14)
                )
                viewModel.updateGpioStates(gpioPacket)

                mainHandler.postDelayed(this, MOCK_UPDATE_INTERVAL_MS)
            }
        }

        mockRunnable = task
        mainHandler.post(task)
    }

    private fun stopMockStream() {
        mockRunnable?.let { mainHandler.removeCallbacks(it) }
        mockRunnable = null
    }

    private fun waveShort(t: Double, amplitude: Double, phase: Double): Short {
        return (sin(t * 0.14 + phase) * amplitude).roundToInt().toShort()
    }

    private fun squareBit(t: Double, divisor: Int): Int {
        return if ((t.roundToInt() / divisor) % 2 == 0) 0 else 1
    }

    private fun TelemetryPacket.toEspData(): EspData {
        val formattedTimestamp = String.format(
            Locale.getDefault(),
            "%02d:%02d:%02d",
            rtcHour,
            rtcMin,
            rtcSec
        )
        val batteryText = if (bat == 0) "N/A" else "$bat%"

        return EspData(
            temperature = temp,
            ioLog = "Telemetry packet",
            timestamp = formattedTimestamp,
            rssi = 0,
            networkName = "WebSocket",
            cpuLoad = cpu.toFloat(),
            voltage = volt,
            currentNow = curr,
            currentTotal = curr,
            batteryLife = batteryText,
            gpioPin = "RTC",
            batteryPercentage = bat.toFloat()
        )
    }

    private fun GpioPacket.toLogSummary(): String {
        return "g4=$gpio4 g3=$gpio3 g2=$gpio2 g1=$gpio1 g0=$gpio0 g21=$gpio21 g20=$gpio20 g10=$gpio10 g9=$gpio9 g8=$gpio8 g7=$gpio7 g6=$gpio6 g5=$gpio5"
    }
}

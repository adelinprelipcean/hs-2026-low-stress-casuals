package com.example.esp32_diagtool

import android.os.Bundle
import android.util.Log
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.viewpager2.adapter.FragmentStateAdapter
import com.example.esp32_diagtool.databinding.ActivityMainBinding
import com.example.esp32_diagtool.fragments.MainDashboardFragment
import com.example.esp32_diagtool.fragments.ThreeDFragment
import com.example.esp32_diagtool.model.EspData
import com.example.esp32_diagtool.model.TelemetryPacket
import com.example.esp32_diagtool.network.SocketManager
import com.google.android.material.tabs.TabLayoutMediator
import java.util.Locale

class MainActivity : AppCompatActivity() {
    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var binding: ActivityMainBinding
    private val viewModel: MainViewModel by viewModels()
    private lateinit var socketManager: SocketManager
    private var imuCallbackCount = 0L
    private var telemetryCallbackCount = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)
        supportActionBar?.title = getString(R.string.app_name)

        setupViewPager()
        initSocket()
    }

    private fun setupViewPager() {
        val adapter = object : FragmentStateAdapter(this) {
            override fun getItemCount(): Int = 2
            
            override fun createFragment(position: Int): Fragment {
                return when (position) {
                    0 -> ThreeDFragment()
                    else -> MainDashboardFragment()
                }
            }
        }
        binding.viewPager.adapter = adapter
        binding.viewPager.offscreenPageLimit = 2
        binding.viewPager.isUserInputEnabled = true

        TabLayoutMediator(binding.tabLayout, binding.viewPager) { tab, position ->
            tab.text = when (position) {
                0 -> getString(R.string.tab_3d_model)
                else -> getString(R.string.tab_esp_data)
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
            onConnectionChanged = { connected ->
                Log.d(TAG, "Socket connection status: $connected")
            }
        )
        socketManager.connect()
    }

    override fun onDestroy() {
        super.onDestroy()
        socketManager.disconnect()
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
}

package com.example.esp32_diagtool

import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.TextView
import androidx.activity.OnBackPressedCallback
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.example.esp32_diagtool.databinding.ActivityMainBinding
import com.example.esp32_diagtool.fragments.LogFragment
import com.example.esp32_diagtool.model.EspData
import com.example.esp32_diagtool.mqtt.MqttManager
import com.example.esp32_diagtool.utils.PreferenceManager
import com.example.esp32_diagtool.utils.RoundedBackgroundSpan
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.util.Locale
import androidx.core.graphics.toColorInt
import androidx.core.view.isVisible

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var preferenceManager: PreferenceManager
    private val viewModel: MainViewModel by viewModels()
    private lateinit var mqttManager: MqttManager

    private val themePurple = "#D0BCFF".toColorInt()
    
    private val offlineHandler = Handler(Looper.getMainLooper())
    private val offlineRunnable = Runnable {
        showOfflineState()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)
        preferenceManager = PreferenceManager(this)

        setupCharts()
        setupListeners()
        
        // Populate existing data if we are recovering from a config change
        populateExistingData()
        
        startMqtt()
        
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (binding.fragmentContainer.isVisible) {
                    hideLogFragment()
                } else {
                    isEnabled = false
                    onBackPressedDispatcher.onBackPressed()
                }
            }
        })

        supportFragmentManager.addOnBackStackChangedListener {
            updateToolbar()
        }
    }

    private fun populateExistingData() {
        val history = viewModel.logHistory.value ?: return
        if (history.isEmpty()) {
            showInitialState()
            return
        }
        
        val startTime = viewModel.startTime
        if (startTime == -1L) return

        showDataState()

        history.forEach { point ->
            val time = (point.receivedAt - startTime) / 1000f
            val data = point.data
            
            // We don't need to update all UI here, just the charts
            val displayTemp = if (preferenceManager.isFahrenheit) {
                (data.temperature * 9/5) + 32
            } else {
                data.temperature
            }
            
            addEntry(binding.chartTemperature, time, displayTemp)
            addEntry(binding.chartLight, time, data.lightIntensity)
            addEntry(binding.chartVoltage, time, data.voltage)
            addEntry(binding.chartCurrent, time, data.currentNow)
        }
        
        // Update the textual UI with the latest data
        history.lastOrNull()?.let { updateTextualUI(it.data) }
        
        resetOfflineTimer()
    }

    private fun showInitialState() {
        binding.tvEmptyMessage.text = getString(R.string.msg_waiting_device)
        binding.tvEmptyMessage.visibility = View.VISIBLE
        binding.nestedScrollView.visibility = View.GONE
    }

    private fun showDataState() {
        binding.tvEmptyMessage.visibility = View.GONE
        binding.nestedScrollView.visibility = View.VISIBLE
    }

    private fun showOfflineState() {
        binding.tvEmptyMessage.text = getString(R.string.msg_device_offline)
        binding.tvEmptyMessage.visibility = View.VISIBLE
        binding.nestedScrollView.visibility = View.GONE
    }

    private fun resetOfflineTimer() {
        offlineHandler.removeCallbacks(offlineRunnable)
        offlineHandler.postDelayed(offlineRunnable, 10000)
    }

    private fun updateToolbar() {
        val isLogVisible = binding.fragmentContainer.isVisible
        supportActionBar?.setDisplayHomeAsUpEnabled(isLogVisible)
        supportActionBar?.title = if (isLogVisible) getString(R.string.log_history) else getString(R.string.app_name)
        invalidateOptionsMenu()
    }

    override fun onSupportNavigateUp(): Boolean {
        if (binding.fragmentContainer.isVisible) {
            hideLogFragment()
            return true
        }
        return super.onSupportNavigateUp()
    }

    private fun setupListeners() {
        binding.cardLog.setOnClickListener {
            showLogFragment()
        }
    }

    private fun showLogFragment() {
        supportFragmentManager.beginTransaction()
            .replace(R.id.fragmentContainer, LogFragment())
            .addToBackStack(null)
            .commit()
        binding.nestedScrollView.visibility = View.GONE
        binding.tvEmptyMessage.visibility = View.GONE
        binding.fragmentContainer.visibility = View.VISIBLE
        updateToolbar()
    }

    private fun hideLogFragment() {
        supportFragmentManager.popBackStack()
        binding.fragmentContainer.visibility = View.GONE
        // Restore previous state
        if (viewModel.logHistory.value.isNullOrEmpty()) {
            showInitialState()
        } else {
            showDataState()
        }
        updateToolbar()
    }

    private fun setupCharts() {
        configureChart(binding.chartTemperature, "Temperature", themePurple)
        configureChart(binding.chartLight, "Light Intensity", themePurple)
        configureChart(binding.chartVoltage, "Voltage", themePurple)
        configureChart(binding.chartCurrent, "Current", themePurple)
    }

    private fun configureChart(chart: LineChart, label: String, color: Int) {
        chart.apply {
            description.isEnabled = false
            setTouchEnabled(true)
            setPinchZoom(true)
            setDrawGridBackground(false)
            setBackgroundColor(Color.TRANSPARENT)
            setNoDataText("Awaiting data...")
            setNoDataTextColor(Color.GRAY)

            xAxis.apply {
                textColor = Color.parseColor("#80FFFFFF")
                position = XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(false)
                setDrawAxisLine(false)
            }

            axisLeft.apply {
                textColor = Color.parseColor("#80FFFFFF")
                setDrawGridLines(true)
                gridColor = Color.parseColor("#1AFFFFFF")
                setDrawAxisLine(false)
            }

            axisRight.isEnabled = false
            legend.isEnabled = false
        }
        
        val dataSet = LineDataSet(mutableListOf(), label).apply {
            this.color = color
            setCircleColor(color)
            lineWidth = 2.5f
            setDrawValues(false)
            setDrawCircles(false)
            mode = LineDataSet.Mode.LINEAR
            setDrawFilled(true)
            
            val gradientDrawable = GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(
                    adjustAlpha(color, 0.3f),
                    adjustAlpha(color, 0.0f)
                )
            )
            fillDrawable = gradientDrawable
            setDrawHorizontalHighlightIndicator(false)
            setDrawVerticalHighlightIndicator(true)
            highLightColor = Color.WHITE
        }
        
        chart.data = LineData(dataSet)
    }

    private fun adjustAlpha(color: Int, factor: Float): Int {
        val alpha = Math.round(Color.alpha(color) * factor)
        val red = Color.red(color)
        val green = Color.green(color)
        val blue = Color.blue(color)
        return Color.argb(alpha, red, green, blue)
    }

    private fun startMqtt() {
        mqttManager = MqttManager(
            onDataReceived = { data ->
                runOnUiThread {
                    showDataState()
                    updateUI(data)
                    viewModel.updateData(data)
                    resetOfflineTimer()
                }
            },
            onConnectionChanged = { _ ->
                // Scraping old error view logic
            }
        )
        Thread { mqttManager.connect() }.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        offlineHandler.removeCallbacks(offlineRunnable)
        if (::mqttManager.isInitialized) {
            Thread { mqttManager.disconnect() }.start()
        }
    }

    private fun updateUI(data: EspData) {
        val now = System.currentTimeMillis()
        if (viewModel.startTime == -1L) {
            viewModel.startTime = now
        }
        val time = (now - viewModel.startTime) / 1000f

        updateTextualUI(data)

        // Temperature Graph
        val displayTemp = if (preferenceManager.isFahrenheit) {
            (data.temperature * 9/5) + 32
        } else {
            data.temperature
        }
        addEntry(binding.chartTemperature, time, displayTemp)

        // Light Graph
        addEntry(binding.chartLight, time, data.lightIntensity)

        // Voltage & Current Graphs
        addEntry(binding.chartVoltage, time, data.voltage)
        addEntry(binding.chartCurrent, time, data.currentNow)
    }

    private fun updateTextualUI(data: EspData) {
        // Battery Card
        binding.tvBatteryValue.text = getString(R.string.battery_percentage_format, data.batteryPercentage)
        if (data.batteryLife == "N/A") {
            binding.ivBatteryIcon.setImageResource(android.R.drawable.ic_lock_idle_charging)
            binding.tvBatteryStatus.text = getString(R.string.status_plugged_in)
            binding.tvBatteryLife.text = getString(R.string.status_plugged_in)
        } else {
            binding.ivBatteryIcon.setImageResource(android.R.drawable.ic_lock_idle_low_battery)
            binding.tvBatteryStatus.text = getString(R.string.label_battery)
            binding.tvBatteryLife.text = data.batteryLife
        }
        
        // Temperature
        val displayTemp = if (preferenceManager.isFahrenheit) {
            (data.temperature * 9/5) + 32
        } else {
            data.temperature
        }
        val unit = if (preferenceManager.isFahrenheit) "°F" else "°C"
        binding.tvTemperature.text = TextFormatter.formatFloat(displayTemp, 1, unit)

        // Light
        binding.tvLight.text = TextFormatter.formatFloat(data.lightIntensity, 1, "Lux")

        // CPU (Circular)
        binding.tvCpuLoad.text = TextFormatter.formatFloat(data.cpuLoad, 1, "%")
        binding.progressCpu.progress = data.cpuLoad.toInt()

        // Voltage & Current
        binding.tvVoltage.text = TextFormatter.formatFloat(data.voltage, 2, "V")
        binding.tvCurrentNow.text = TextFormatter.formatFloat(data.currentNow, 2, "mA")

        // WiFi (Circular RSSI)
        binding.tvNetwork.text = data.networkName
        binding.tvRSSI.text = String.format(Locale.getDefault(), "%d dBm", data.rssi)
        val rssiPercent = ((data.rssi + 100) * 2).coerceIn(0, 100)
        binding.progressRssi.progress = rssiPercent

        // Log with Flares
        updateLogView(data)
    }

    private fun updateLogView(data: EspData) {
        val timestamp = "${data.timestamp}"
        val gpio = "${data.gpioPin} "
        val message = "  ${data.ioLog}\n"

        val builder = SpannableStringBuilder()
        
        // Timestamp Flare
        val tsStart = builder.length
        builder.append(timestamp)
        builder.setSpan(
            RoundedBackgroundSpan(
                ContextCompat.getColor(this, R.color.primary_container_light),
                ContextCompat.getColor(this, R.color.on_primary_container_light),
                16f,
                12f
            ),
            tsStart,
            builder.length,
            Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        
        builder.append(" ")

        // GPIO Flare
        val gpioStart = builder.length
        builder.append(gpio)
        builder.setSpan(
            RoundedBackgroundSpan(
                ContextCompat.getColor(this, R.color.tertiary_light),
                ContextCompat.getColor(this, R.color.on_secondary_light),
                16f,
                12f
            ),
            gpioStart,
            builder.length,
            Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
        )

        builder.append(message)
        
        val currentLog = binding.tvLog.text
        if (currentLog.toString() != getString(R.string.waiting_data)) {
            builder.append(currentLog)
        }
        
        val finalContent = if (builder.length > 2000) builder.subSequence(0, 2000) else builder
        binding.tvLog.setText(finalContent, TextView.BufferType.SPANNABLE)
    }

    private fun addEntry(chart: LineChart, x: Float, y: Float) {
        val data = chart.data
        if (data != null) {
            var set = data.getDataSetByIndex(0) as? LineDataSet
            if (set == null) {
                set = LineDataSet(mutableListOf(), "Data")
                data.addDataSet(set)
            }
            
            // Avoid adding duplicate or older entries to the chart to prevent "weird forms"
            val lastEntry = set.values.lastOrNull()
            if (lastEntry != null && lastEntry.x >= x) {
                return 
            }

            data.addEntry(Entry(x, y), 0)
            data.notifyDataChanged()
            chart.notifyDataSetChanged()
            chart.setVisibleXRangeMaximum(30f)
            chart.moveViewToX(x)
            chart.invalidate()
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        if (binding.fragmentContainer.visibility == View.VISIBLE) {
            return false // LogFragment will provide its own menu
        }
        menu.add(Menu.NONE, 1, Menu.NONE, getString(R.string.settings))
            .setIcon(android.R.drawable.ic_menu_preferences)
            .setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == 1) {
            startActivity(Intent(this, SettingsActivity::class.java))
            return true
        }
        return super.onOptionsItemSelected(item)
    }
}

package com.example.esp32_diagtool.fragments

import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.FragmentMainBinding
import com.example.esp32_diagtool.model.EspData
import com.example.esp32_diagtool.utils.PreferenceManager
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet

class MainDashboardFragment : Fragment() {
    companion object {
        private const val TAG = "MainDashboardFragment"
    }

    private var _binding: FragmentMainBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var preferenceManager: PreferenceManager
    private var observedTelemetryCount = 0L

    private val themePurple = Color.parseColor("#D0BCFF")

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentMainBinding.inflate(inflater, container, false)
        preferenceManager = PreferenceManager(requireContext())
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        setupCharts()
        
        viewModel.logHistory.observe(viewLifecycleOwner) { history ->
            binding.nestedScrollView.visibility = View.VISIBLE
            if (history.isNotEmpty()) {
                observedTelemetryCount++
                if (observedTelemetryCount <= 3L || observedTelemetryCount % 50L == 0L) {
                    val latest = history.last().data
                    Log.d(
                        TAG,
                        "Observed ESP #$observedTelemetryCount history=${history.size} temp=${latest.temperature} volt=${latest.voltage} curr=${latest.currentNow}"
                    )
                }
                updateData(history.last().data)
            }
        }
    }

    private fun setupCharts() {
        configureChart(binding.chartTemperature, "Temperature", getTemperatureColor(24f))
        configureChart(binding.chartVoltage, "Voltage", themePurple)
        configureChart(binding.chartCurrent, "Current", themePurple)
    }

    private fun configureChart(chart: LineChart, label: String, color: Int) {
        configureChartBase(chart)
        
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

    private fun configureChartBase(chart: LineChart) {
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
    }

    private fun adjustAlpha(color: Int, factor: Float): Int {
        val alpha = Math.round(Color.alpha(color) * factor)
        val red = Color.red(color)
        val green = Color.green(color)
        val blue = Color.blue(color)
        return Color.argb(alpha, red, green, blue)
    }

    fun updateData(data: EspData) {
        if (_binding == null) return
        
        val now = System.currentTimeMillis()
        if (viewModel.startTime == -1L) {
            viewModel.startTime = now
        }
        val time = (now - viewModel.startTime) / 1000f

        updateAllUI(data)

        // Temperature Graph
        val displayTemp = if (preferenceManager.isFahrenheit) {
            (data.temperature * 9/5) + 32
        } else {
            data.temperature
        }
        addTemperatureEntry(binding.chartTemperature, time, displayTemp, data.temperature)
        addEntry(binding.chartVoltage, time, data.voltage)
        addEntry(binding.chartCurrent, time, data.currentNow)
    }

    private fun updateAllUI(data: EspData) {
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
        
        val displayTemp = if (preferenceManager.isFahrenheit) {
            (data.temperature * 9/5) + 32
        } else {
            data.temperature
        }
        val unit = if (preferenceManager.isFahrenheit) "°F" else "°C"
        binding.tvTemperature.text = TextFormatter.formatFloat(displayTemp, 1, unit)
        binding.tvCpuLoad.text = TextFormatter.formatFloat(data.cpuLoad, 1, "%")
        binding.progressCpu.progress = data.cpuLoad.toInt()
        binding.progressCpu.setIndicatorColor(getCpuColor(data.cpuLoad))
        binding.tvVoltage.text = TextFormatter.formatFloat(data.voltage, 2, "V")
        binding.tvCurrentNow.text = TextFormatter.formatFloat(data.currentNow, 2, "mA")
    }

    private fun getCpuColor(cpuLoad: Float): Int {
        val clamped = cpuLoad.coerceIn(0f, 100f)
        val hue = 120f - (120f * (clamped / 100f))
        val hsv = floatArrayOf(hue, 0.85f, 0.95f)
        return Color.HSVToColor(hsv)
    }

    private fun getTemperatureColor(tempCelsius: Float): Int {
        val normalized = ((tempCelsius - 10f) / 28f).coerceIn(0f, 1f)
        val hue = 240f - (240f * normalized)
        val hsv = floatArrayOf(hue, 0.9f, 0.98f)
        return Color.HSVToColor(hsv)
    }

    private fun addEntry(chart: LineChart, x: Float, y: Float) {
        val data = chart.data
        if (data != null) {
            var set = data.getDataSetByIndex(0) as? LineDataSet
            if (set == null) {
                set = LineDataSet(mutableListOf(), "Data")
                data.addDataSet(set)
            }

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

    private fun addTemperatureEntry(chart: LineChart, x: Float, y: Float, sourceTempCelsius: Float) {
        val data = chart.data ?: return
        val set = data.getDataSetByIndex(0) as? LineDataSet ?: return

        val lastEntry = set.values.lastOrNull()
        if (lastEntry != null && lastEntry.x >= x) {
            return
        }

        set.color = getTemperatureColor(sourceTempCelsius)
        set.setCircleColor(set.color)
        data.addEntry(Entry(x, y), 0)

        data.notifyDataChanged()
        chart.notifyDataSetChanged()
        chart.setVisibleXRangeMaximum(30f)
        chart.moveViewToX(x)
        chart.invalidate()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

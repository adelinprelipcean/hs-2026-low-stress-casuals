package com.example.esp32_diagtool.fragments

import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentEnvironmentBinding
import com.example.esp32_diagtool.utils.PreferenceManager
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet

class EnvironmentFragment : Fragment() {

    private var _binding: FragmentEnvironmentBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var preferenceManager: PreferenceManager
    private var startTime = System.currentTimeMillis()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentEnvironmentBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        preferenceManager = PreferenceManager(requireContext())
        setupCharts()

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            val time = (System.currentTimeMillis() - startTime) / 1000f
            
            // Temperature
            val displayTemp = if (preferenceManager.isFahrenheit) {
                (data.temperature * 9/5) + 32
            } else {
                data.temperature
            }
            val unit = if (preferenceManager.isFahrenheit) "°F" else "°C"
            binding.tvTemperature.text = TextFormatter.formatFloat(displayTemp, 1, unit)
            addEntry(binding.chartTemperature, time, displayTemp)

            // Light
            binding.tvLight.text = TextFormatter.formatFloat(data.lightIntensity, 1, "Lux")
            addEntry(binding.chartLight, time, data.lightIntensity)
        }
    }

    private fun setupCharts() {
        configureChart(binding.chartTemperature, "Temperature", Color.parseColor("#FF5252"))
        configureChart(binding.chartLight, "Light Intensity", Color.parseColor("#FFD740"))
    }

    private fun configureChart(chart: LineChart, label: String, color: Int) {
        chart.apply {
            description.isEnabled = false
            setTouchEnabled(true)
            setPinchZoom(true)
            setDrawGridBackground(false)
            setBackgroundColor(Color.TRANSPARENT)
            setNoDataText("Awaiting sensor data...")
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
            lineWidth = 3f
            setDrawValues(false)
            setDrawCircles(false)
            mode = LineDataSet.Mode.CUBIC_BEZIER
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

    private fun addEntry(chart: LineChart, x: Float, y: Float) {
        val data = chart.data
        if (data != null) {
            var set = data.getDataSetByIndex(0) as? LineDataSet
            if (set == null) {
                set = LineDataSet(mutableListOf(), "Data")
                data.addDataSet(set)
            }
            data.addEntry(Entry(x, y), 0)
            data.notifyDataChanged()
            chart.notifyDataSetChanged()
            chart.setVisibleXRangeMaximum(30f)
            chart.moveViewToX(x)
            chart.invalidate()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

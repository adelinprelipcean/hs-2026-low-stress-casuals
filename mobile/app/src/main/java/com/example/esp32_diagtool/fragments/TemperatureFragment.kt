package com.example.esp32_diagtool.fragments

import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentTemperatureBinding
import com.example.esp32_diagtool.utils.PreferenceManager
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet

class TemperatureFragment : Fragment() {
    private var _binding: FragmentTemperatureBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var preferenceManager: PreferenceManager
    private var startTime = System.currentTimeMillis()
    private var minTemp = Float.MAX_VALUE
    private var maxTemp = Float.MIN_VALUE

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentTemperatureBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        preferenceManager = PreferenceManager(requireContext())
        setupChart(binding.chartTempLarge)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            val time = (System.currentTimeMillis() - startTime) / 1000f
            val displayTemp = if (preferenceManager.isFahrenheit) (data.temperature * 9/5) + 32 else data.temperature
            val unit = if (preferenceManager.isFahrenheit) "°F" else "°C"

            binding.tvTempValue.text = TextFormatter.formatFloat(displayTemp, 1, unit)
            
            if (displayTemp < minTemp) minTemp = displayTemp
            if (displayTemp > maxTemp) maxTemp = displayTemp
            
            binding.tvTempMin.text = TextFormatter.formatFloat(minTemp, 1, unit)
            binding.tvTempMax.text = TextFormatter.formatFloat(maxTemp, 1, unit)
            
            binding.tvTempStatus.text = when {
                data.temperature > 40 -> "High Temperature"
                data.temperature < 15 -> "Low Temperature"
                else -> "Normal Range"
            }

            addEntry(binding.chartTempLarge, time, displayTemp)
        }
    }

    private fun setupChart(chart: LineChart) {
        chart.apply {
            description.isEnabled = false
            xAxis.position = XAxis.XAxisPosition.BOTTOM
            axisRight.isEnabled = false
            legend.isEnabled = false
        }
        val set = LineDataSet(mutableListOf(), "Temp").apply {
            color = Color.RED
            setDrawCircles(false)
            lineWidth = 2f
            mode = LineDataSet.Mode.CUBIC_BEZIER
        }
        chart.data = LineData(set)
    }

    private fun addEntry(chart: LineChart, x: Float, y: Float) {
        val data = chart.data
        data.addEntry(Entry(x, y), 0)
        data.notifyDataChanged()
        chart.notifyDataSetChanged()
        chart.setVisibleXRangeMaximum(60f)
        chart.moveViewToX(x)
        chart.invalidate()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

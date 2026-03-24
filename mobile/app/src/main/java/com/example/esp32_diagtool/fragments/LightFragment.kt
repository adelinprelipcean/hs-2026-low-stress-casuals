package com.example.esp32_diagtool.fragments

import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentLightBinding
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet

class LightFragment : Fragment() {
    private var _binding: FragmentLightBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private var startTime = System.currentTimeMillis()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentLightBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        setupChart(binding.chartLightLarge)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            val time = (System.currentTimeMillis() - startTime) / 1000f
            binding.tvLightValue.text = TextFormatter.formatFloat(data.lightIntensity, 1, "Lux")
            
            binding.tvLightStatus.text = when {
                data.lightIntensity > 1000 -> "Direct Sunlight"
                data.lightIntensity > 300 -> "Bright Room"
                data.lightIntensity > 50 -> "Dim Light"
                else -> "Dark"
            }

            addEntry(binding.chartLightLarge, time, data.lightIntensity)
        }
    }

    private fun setupChart(chart: LineChart) {
        chart.apply {
            description.isEnabled = false
            xAxis.position = XAxis.XAxisPosition.BOTTOM
            axisRight.isEnabled = false
            legend.isEnabled = false
        }
        val set = LineDataSet(mutableListOf(), "Light").apply {
            color = Color.parseColor("#FFD740")
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

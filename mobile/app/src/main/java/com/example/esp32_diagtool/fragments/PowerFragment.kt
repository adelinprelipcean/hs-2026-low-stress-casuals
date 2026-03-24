package com.example.esp32_diagtool.fragments

import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentPowerBinding
import com.example.esp32_diagtool.utils.TextFormatter
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.util.Locale

class PowerFragment : Fragment() {
    private var _binding: FragmentPowerBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private var startTime = System.currentTimeMillis()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentPowerBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        setupChart(binding.chartPower)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            val time = (System.currentTimeMillis() - startTime) / 1000f
            
            binding.tvVoltageValue.text = TextFormatter.formatFloat(data.voltage, 2, "V")
            binding.tvBatteryLife.text = String.format("Battery: %s", data.batteryLife)
            binding.tvCurrentNow.text = TextFormatter.formatFloat(data.currentNow, 2, "mA")
            binding.tvCurrentTotal.text = TextFormatter.formatFloat(data.currentTotal, 2, "mAh")
            
            val powerWatts = data.voltage * (data.currentNow / 1000f) * 1000f // in mW
            binding.tvPowerWatts.text = TextFormatter.formatFloat(powerWatts, 1, "mW")

            addEntry(binding.chartPower, time, data.voltage)
        }
    }

    private fun setupChart(chart: LineChart) {
        chart.apply {
            description.isEnabled = false
            xAxis.position = XAxis.XAxisPosition.BOTTOM
            axisRight.isEnabled = false
            legend.isEnabled = false
        }
        val set = LineDataSet(mutableListOf(), "Voltage").apply {
            color = Color.parseColor("#4CAF50")
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

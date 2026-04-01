package com.example.esp32_diagtool.fragments

import android.content.res.ColorStateList
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.FragmentIoBinding
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet

class IoFragment : Fragment() {

    companion object {
        private const val HISTORY_SIZE = 36
    }

    private var _binding: FragmentIoBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    private val indicatorByPin: Map<String, (FragmentIoBinding) -> View> = mapOf(
        "GPIO4" to { it.indicatorGpio4 },
        "GPIO3" to { it.indicatorGpio3 },
        "GPIO2" to { it.indicatorGpio2 },
        "GPIO1" to { it.indicatorGpio1 },
        "GPIO0" to { it.indicatorGpio0 },
        "GPIO21" to { it.indicatorGpio21 },
        "GPIO20" to { it.indicatorGpio20 },
        "GPIO10" to { it.indicatorGpio10 },
        "GPIO9" to { it.indicatorGpio9 },
        "GPIO8" to { it.indicatorGpio8 },
        "GPIO7" to { it.indicatorGpio7 },
        "GPIO6" to { it.indicatorGpio6 },
        "GPIO5" to { it.indicatorGpio5 }
    )

    private val graphPinOrder = listOf(
        "GPIO5", "GPIO9", "5V", "GPIO3",
        "GPIO6", "GPIO10", "GND", "GPIO2",
        "GPIO7", "GPIO20", "3.3V", "GPIO1",
        "GPIO8", "GPIO21", "GPIO4", "GPIO0"
    )

    private val pinGraphViews = mutableMapOf<String, LineChart>()
    private val pinHistories = mutableMapOf<String, ArrayDeque<Boolean>>()
    private val pinChartIndices = mutableMapOf<String, Int>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentIoBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        initializeHistoryBuffers()
        setupGraphGrid()
        loadBoardImage()
        applyStaticPins()
        updateGraphHistory(viewModel.gpioStates.value.orEmpty())

        viewModel.gpioStates.observe(viewLifecycleOwner) { states ->
            updateDynamicPins(states)
            updateGraphHistory(states)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun loadBoardImage() {
        requireContext().assets.open("esp32_c3_mini.png").use { input ->
            binding.ivBoard.setImageBitmap(BitmapFactory.decodeStream(input))
        }
    }

    private fun applyStaticPins() {
        setIndicatorState(binding.indicator5v, isHigh = true)
        setIndicatorState(binding.indicatorGnd, isHigh = false)
        setIndicatorState(binding.indicator3v3, isHigh = true)
    }

    private fun initializeHistoryBuffers() {
        pinHistories.clear()
        pinChartIndices.clear()
        graphPinOrder.forEach { pinName ->
            pinHistories[pinName] = ArrayDeque(HISTORY_SIZE)
            pinChartIndices[pinName] = 0
        }
    }

    private fun setupGraphGrid() {
        val currentBinding = _binding ?: return
        pinGraphViews.clear()
        currentBinding.pinGraphsGrid.removeAllViews()

        graphPinOrder.forEach { pinName ->
            val itemView = layoutInflater.inflate(
                R.layout.item_pin_graph,
                currentBinding.pinGraphsGrid,
                false
            )
            itemView.findViewById<TextView>(R.id.tvPinName).text = pinName
            val chart = itemView.findViewById<LineChart>(R.id.pinLineChart)
            configureLineChart(chart, pinName)
            pinGraphViews[pinName] = chart
            currentBinding.pinGraphsGrid.addView(itemView)
        }
    }

    private fun configureLineChart(chart: LineChart, pinName: String) {
        chart.apply {
            description.isEnabled = false
            setTouchEnabled(false)
            setDrawGridBackground(false)
            setBackgroundColor(Color.TRANSPARENT)

            xAxis.apply {
                textColor = Color.parseColor("#80FFFFFF")
                position = XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(false)
                setDrawAxisLine(false)
            }

            axisLeft.apply {
                textColor = Color.parseColor("#80FFFFFF")
                setDrawGridLines(false)
                setDrawAxisLine(false)
                axisMinimum = -0.2f
                axisMaximum = 1.2f
                setLabelCount(2, false)
                valueFormatter = object : com.github.mikephil.charting.formatter.ValueFormatter() {
                    override fun getFormattedValue(value: Float): String {
                        return if (value < 0.5f) "0" else "1"
                    }
                }
            }

            axisRight.isEnabled = false
            legend.isEnabled = false
        }

        val dataSet = LineDataSet(mutableListOf(), pinName).apply {
            color = Color.parseColor("#FF6B6B")
            lineWidth = 2f
            setDrawValues(false)
            setDrawCircles(false)
            mode = LineDataSet.Mode.STEPPED
            setDrawFilled(false)
            setDrawHorizontalHighlightIndicator(false)
            setDrawVerticalHighlightIndicator(false)
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

    private fun updateGraphHistory(states: Map<String, Boolean>) {
        val fullState = states.toMutableMap().apply {
            this["5V"] = true
            this["GND"] = false
            this["3.3V"] = true
        }

        graphPinOrder.forEach { pinName ->
            val isHigh = fullState[pinName] == true
            appendHistorySample(pinName, isHigh)
            updateChart(pinName)
        }
    }

    private fun appendHistorySample(pinName: String, isHigh: Boolean) {
        val history = pinHistories[pinName] ?: return
        if (history.size >= HISTORY_SIZE) {
            history.removeFirst()
        }
        history.addLast(isHigh)
    }

    private fun updateChart(pinName: String) {
        val chart = pinGraphViews[pinName] ?: return
        val history = pinHistories[pinName] ?: return
        
        val entries = mutableListOf<Entry>()
        val colors = mutableListOf<Int>()
        
        val redColor = Color.parseColor("#FF6B6B")
        val grayColor = Color.parseColor("#808080")

        history.forEachIndexed { index, value ->
            entries.add(Entry(index.toFloat(), if (value) 1f else 0f))
            colors.add(if (value) redColor else grayColor)
        }

        val dataSet = chart.data.getDataSetByIndex(0) as LineDataSet
        dataSet.values = entries
        dataSet.setColors(colors)
        
        chart.data.notifyDataChanged()
        chart.notifyDataSetChanged()
        chart.invalidate()
    }

    private fun updateDynamicPins(states: Map<String, Boolean>) {
        val currentBinding = _binding ?: return
        indicatorByPin.forEach { (pinName, resolver) ->
            setIndicatorState(resolver(currentBinding), states[pinName] == true)
        }
    }

    private fun setIndicatorState(indicator: View, isHigh: Boolean) {
        val colorRes = if (isHigh) R.color.pin_high_red else R.color.pin_low_gray
        val color = ContextCompat.getColor(requireContext(), colorRes)
        indicator.backgroundTintList = ColorStateList.valueOf(color)
    }
}

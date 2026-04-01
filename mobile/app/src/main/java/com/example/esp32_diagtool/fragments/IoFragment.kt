package com.example.esp32_diagtool.fragments

import android.content.res.ColorStateList
import android.graphics.BitmapFactory
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
import com.example.esp32_diagtool.views.PinHistoryGraphView

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
        "GPIO4", "GPIO3", "GPIO2", "GPIO1",
        "GPIO0", "GPIO21", "GPIO20", "GPIO10",
        "5V", "GND", "3.3V", "GPIO9",
        "GPIO8", "GPIO7", "GPIO6", "GPIO5"
    )

    private val pinGraphViews = mutableMapOf<String, PinHistoryGraphView>()
    private val pinHistories = mutableMapOf<String, ArrayDeque<Boolean>>()

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
        graphPinOrder.forEach { pinName ->
            pinHistories[pinName] = ArrayDeque(HISTORY_SIZE)
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
            val graphView = itemView.findViewById<PinHistoryGraphView>(R.id.pinHistoryGraph)
            pinGraphViews[pinName] = graphView
            currentBinding.pinGraphsGrid.addView(itemView)
        }
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
            pinGraphViews[pinName]?.setHistory(pinHistories[pinName].orEmpty().toList())
        }
    }

    private fun appendHistorySample(pinName: String, isHigh: Boolean) {
        val history = pinHistories[pinName] ?: return
        if (history.size >= HISTORY_SIZE) {
            history.removeFirst()
        }
        history.addLast(isHigh)
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

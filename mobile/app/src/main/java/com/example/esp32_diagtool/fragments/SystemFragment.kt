package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentSystemBinding
import com.example.esp32_diagtool.utils.TextFormatter

class SystemFragment : Fragment() {

    private var _binding: FragmentSystemBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentSystemBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            binding.tvNetwork.text = data.networkName
            binding.tvRSSI.text = "${data.rssi} dBm"
            binding.tvCpuLoad.text = TextFormatter.formatFloat(data.cpuLoad, 1, "%")
            binding.tvVoltage.text = TextFormatter.formatFloat(data.voltage, 2, "V")
            binding.tvCurrentNow.text = TextFormatter.formatFloat(data.currentNow, 2, "mA")
            binding.tvCurrentTotal.text = TextFormatter.formatFloat(data.currentTotal, 2, "mAh")
            binding.tvBatteryLife.text = data.batteryLife
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

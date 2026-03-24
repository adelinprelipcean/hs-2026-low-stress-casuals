package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentHomeBinding
import com.example.esp32_diagtool.utils.PreferenceManager
import com.example.esp32_diagtool.utils.TextFormatter

class HomeFragment : Fragment() {

    private var _binding: FragmentHomeBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var preferenceManager: PreferenceManager

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentHomeBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        preferenceManager = PreferenceManager(requireContext())

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            val displayTemp = if (preferenceManager.isFahrenheit) {
                (data.temperature * 9/5) + 32
            } else {
                data.temperature
            }
            val unit = if (preferenceManager.isFahrenheit) "°F" else "°C"
            
            binding.tvTemp.text = TextFormatter.formatFloat(displayTemp, 1, unit)
            binding.tvLight.text = TextFormatter.formatFloat(data.lightIntensity, 1, "Lux")
            binding.tvCpu.text = TextFormatter.formatFloat(data.cpuLoad, 1, "%")
            binding.tvVoltage.text = TextFormatter.formatFloat(data.voltage, 2, "V")
            binding.tvNetwork.text = data.networkName
            binding.tvRssi.text = "${data.rssi} dBm"

            val currentLog = binding.tvHomeLog.text.toString()
            val logLine = "[${data.timestamp}] ${data.ioLog}\n"
            binding.tvHomeLog.text = (logLine + currentLog).take(1000)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

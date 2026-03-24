package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentWifiBinding
import java.util.Locale

class WifiFragment : Fragment() {
    private var _binding: FragmentWifiBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentWifiBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            binding.tvSsid.text = data.networkName
            binding.tvRssiValue.text = String.format(Locale.getDefault(), "%d dBm", data.rssi)
            
            // Calculate signal percentage (roughly -100 to -50 range)
            val signalPercent = ((data.rssi + 100) * 2).coerceIn(0, 100)
            binding.progressSignal.progress = signalPercent
            
            // Generated/Relevant info
            binding.tvIpAddress.text = "192.168.1.15" // Placeholder for actual IP if available
            binding.tvChannel.text = "6 (2.4 GHz)"
            binding.tvLinkSpeed.text = "54 Mbps"
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

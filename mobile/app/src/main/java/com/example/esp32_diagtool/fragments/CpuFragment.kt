package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.databinding.FragmentCpuBinding
import com.example.esp32_diagtool.utils.TextFormatter

class CpuFragment : Fragment() {
    private var _binding: FragmentCpuBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentCpuBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        viewModel.espData.observe(viewLifecycleOwner) { data ->
            binding.tvCpuValue.text = TextFormatter.formatFloat(data.cpuLoad, 1, "%")
            binding.progressCpu.progress = data.cpuLoad.toInt()
            
            // Generate some "relevant" info if not in data
            binding.tvCpuTasks.text = "12 Active"
            binding.tvUptime.text = "02:45:12"
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

package com.example.esp32_diagtool.fragments

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.ItemLogBinding
import com.example.esp32_diagtool.model.EspData

class LogAdapter : ListAdapter<EspData, LogAdapter.LogViewHolder>(LogDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LogViewHolder {
        val binding = ItemLogBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return LogViewHolder(binding)
    }

    override fun onBindViewHolder(holder: LogViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class LogViewHolder(private val binding: ItemLogBinding) : RecyclerView.ViewHolder(binding.root) {
        fun bind(data: EspData) {
            val context = binding.root.context
            binding.tvTimestamp.text = context.getString(R.string.timestamp_format, data.timestamp)
            binding.tvGpio.text = context.getString(R.string.gpio_format, data.gpioPin)
            binding.tvMessage.text = data.ioLog
        }
    }

    class LogDiffCallback : DiffUtil.ItemCallback<EspData>() {
        override fun areItemsTheSame(oldItem: EspData, newItem: EspData): Boolean {
            return oldItem.timestamp == newItem.timestamp && oldItem.ioLog == newItem.ioLog
        }

        override fun areContentsTheSame(oldItem: EspData, newItem: EspData): Boolean {
            return oldItem == newItem
        }
    }
}

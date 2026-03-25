package com.example.esp32_diagtool.fragments


import android.text.Spannable
import android.text.SpannableStringBuilder
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.ItemLogBinding
import com.example.esp32_diagtool.EspDataPoint
import com.example.esp32_diagtool.utils.RoundedBackgroundSpan

class LogAdapter : ListAdapter<EspDataPoint, LogAdapter.LogViewHolder>(LogDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LogViewHolder {
        val binding = ItemLogBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return LogViewHolder(binding)
    }

    override fun onBindViewHolder(holder: LogViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class LogViewHolder(private val binding: ItemLogBinding) : RecyclerView.ViewHolder(binding.root) {
        fun bind(dataPoint: EspDataPoint) {
            val data = dataPoint.data
            val context = binding.root.context
            
            // Format Timestamp with Flare
            val tsText = context.getString(R.string.timestamp_format, data.timestamp)
            val tsBuilder = SpannableStringBuilder(tsText)
            tsBuilder.setSpan(
                RoundedBackgroundSpan(
                    ContextCompat.getColor(context, R.color.primary_container_light),
                    ContextCompat.getColor(context, R.color.on_primary_container_light),
                    16f,
                    12f
                ),
                0, tsText.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            binding.tvTimestamp.text = tsBuilder

            // Format GPIO with Flare
            val gpioText = context.getString(R.string.gpio_format, data.gpioPin)
            val gpioBuilder = SpannableStringBuilder(gpioText)
            gpioBuilder.setSpan(
                RoundedBackgroundSpan(
                    ContextCompat.getColor(context, R.color.tertiary_light),
                    ContextCompat.getColor(context, R.color.on_secondary_light),
                    16f,
                    12f
                ),
                0, gpioText.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            binding.tvGpio.text = gpioBuilder

            binding.tvMessage.text = data.ioLog
        }
    }

    class LogDiffCallback : DiffUtil.ItemCallback<EspDataPoint>() {
        override fun areItemsTheSame(oldItem: EspDataPoint, newItem: EspDataPoint): Boolean {
            return oldItem.receivedAt == newItem.receivedAt && oldItem.data.timestamp == newItem.data.timestamp
        }

        override fun areContentsTheSame(oldItem: EspDataPoint, newItem: EspDataPoint): Boolean {
            return oldItem == newItem
        }
    }
}

package com.example.esp32_diagtool

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.example.esp32_diagtool.databinding.ActivitySettingsBinding
import com.example.esp32_diagtool.utils.PreferenceManager

class SettingsActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySettingsBinding
    private lateinit var preferenceManager: PreferenceManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        preferenceManager = PreferenceManager(this)

        binding.etServerUrl.setText(preferenceManager.serverUrl)
        binding.switchUnit.isChecked = preferenceManager.isFahrenheit

        binding.btnSave.setOnClickListener {
            val url = binding.etServerUrl.text.toString()
            if (url.isNotEmpty()) {
                preferenceManager.serverUrl = url
                preferenceManager.isFahrenheit = binding.switchUnit.isChecked
                Toast.makeText(this, "Settings saved", Toast.LENGTH_SHORT).show()
                finish()
            } else {
                Toast.makeText(this, "Please enter a valid URL", Toast.LENGTH_SHORT).show()
            }
        }
    }
}

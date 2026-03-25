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
        binding.switchUnit.isChecked = preferenceManager.isFahrenheit

        binding.btnSave.setOnClickListener {
            preferenceManager.isFahrenheit = binding.switchUnit.isChecked
            Toast.makeText(this, "Settings saved", Toast.LENGTH_SHORT).show()
            finish()
        }
    }
}

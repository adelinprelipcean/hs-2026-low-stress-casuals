package com.example.esp32_diagtool

import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentActivity
import androidx.viewpager2.adapter.FragmentStateAdapter
import com.example.esp32_diagtool.fragments.*

class MainViewPagerAdapter(fragmentActivity: FragmentActivity) : FragmentStateAdapter(fragmentActivity) {
    override fun getItemCount(): Int = 6

    override fun createFragment(position: Int): Fragment {
        return when (position) {
            0 -> HomeFragment()
            1 -> TemperatureFragment()
            2 -> LightFragment()
            3 -> CpuFragment()
            4 -> PowerFragment()
            5 -> WifiFragment()
            else -> HomeFragment()
        }
    }
}

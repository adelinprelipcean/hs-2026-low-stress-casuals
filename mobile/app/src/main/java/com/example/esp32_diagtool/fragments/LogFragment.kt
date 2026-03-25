package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.widget.SearchView
import androidx.core.view.MenuHost
import androidx.core.view.MenuProvider
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.recyclerview.widget.LinearLayoutManager
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.FragmentLogBinding
import com.example.esp32_diagtool.EspDataPoint

class LogFragment : Fragment() {

    private var _binding: FragmentLogBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var logAdapter: LogAdapter
    private var currentFilter: String = ""

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentLogBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        setupMenu()
        setupRecyclerView()

        viewModel.logHistory.observe(viewLifecycleOwner) { history ->
            applyFilter(history)
        }
    }

    private fun setupMenu() {
        val menuHost: MenuHost = requireActivity()
        menuHost.addMenuProvider(object : MenuProvider {
            override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
                val searchItem = menu.add(Menu.NONE, 2, Menu.NONE, getString(R.string.filter))
                searchItem.setIcon(android.R.drawable.ic_menu_search)
                searchItem.setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM or MenuItem.SHOW_AS_ACTION_COLLAPSE_ACTION_VIEW)
                
                val searchView = SearchView(requireContext())
                searchItem.actionView = searchView
                
                searchView.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
                    override fun onQueryTextSubmit(query: String?): Boolean = false

                    override fun onQueryTextChange(newText: String?): Boolean {
                        currentFilter = newText.orEmpty()
                        viewModel.logHistory.value?.let { applyFilter(it) }
                        return true
                    }
                })
            }

            override fun onMenuItemSelected(menuItem: MenuItem): Boolean {
                return false
            }
        }, viewLifecycleOwner, Lifecycle.State.RESUMED)
    }

    private fun setupRecyclerView() {
        logAdapter = LogAdapter()
        binding.rvLogHistory.apply {
            adapter = logAdapter
            layoutManager = LinearLayoutManager(requireContext())
        }
    }

    private fun applyFilter(history: List<EspDataPoint>) {
        val filtered = if (currentFilter.isEmpty()) {
            history
        } else {
            history.filter { 
                it.data.ioLog.contains(currentFilter, ignoreCase = true) || 
                it.data.gpioPin.contains(currentFilter) ||
                it.data.timestamp.contains(currentFilter)
            }
        }
        logAdapter.submitList(filtered.reversed())
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

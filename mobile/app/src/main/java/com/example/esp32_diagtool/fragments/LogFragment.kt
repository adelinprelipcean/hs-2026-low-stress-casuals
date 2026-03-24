package com.example.esp32_diagtool.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.widget.SearchView
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.example.esp32_diagtool.MainViewModel
import com.example.esp32_diagtool.R
import com.example.esp32_diagtool.databinding.FragmentLogBinding

class LogFragment : Fragment() {

    private var _binding: FragmentLogBinding? = null
    private val binding get() = _binding!!
    private val viewModel: MainViewModel by activityViewModels()
    private lateinit var logAdapter: LogAdapter
    private var currentFilter: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setHasOptionsMenu(true)
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentLogBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        setupRecyclerView()

        viewModel.logHistory.observe(viewLifecycleOwner) { history ->
            applyFilter(history)
        }
    }

    private fun setupRecyclerView() {
        logAdapter = LogAdapter()
        binding.rvLogHistory.apply {
            adapter = logAdapter
            layoutManager = LinearLayoutManager(requireContext())
        }
    }

    private fun applyFilter(history: List<com.example.esp32_diagtool.model.EspData>) {
        val filtered = if (currentFilter.isEmpty()) {
            history
        } else {
            history.filter { 
                it.ioLog.contains(currentFilter, ignoreCase = true) || 
                it.gpioPin.toString().contains(currentFilter) ||
                it.timestamp.contains(currentFilter)
            }
        }
        logAdapter.submitList(filtered.reversed())
    }

    override fun onCreateOptionsMenu(menu: Menu, inflater: MenuInflater) {
        val searchItem = menu.add(Menu.NONE, 2, Menu.NONE, getString(R.string.filter))
        searchItem.setIcon(android.R.drawable.ic_menu_search)
        searchItem.setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM or MenuItem.SHOW_AS_ACTION_COLLAPSE_ACTION_VIEW)
        
        val searchView = SearchView(requireContext())
        searchItem.actionView = searchView
        
        searchView.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
            override fun onQueryTextSubmit(query: String?): Boolean {
                return false
            }

            override fun onQueryTextChange(newText: String?): Boolean {
                currentFilter = newText.orEmpty()
                viewModel.logHistory.value?.let { applyFilter(it) }
                return true
            }
        })
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

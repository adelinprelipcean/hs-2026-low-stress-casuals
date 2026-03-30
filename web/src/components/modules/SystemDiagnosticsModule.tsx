import React from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { Activity, Wifi, Cpu } from 'lucide-react';
import { RollingChart } from '../common/RollingChart';

interface SystemDiagnosticsModuleProps {
  currentData: TelemetryData['system'];
  history: TelemetryData[];
}

export const SystemDiagnosticsModule: React.FC<SystemDiagnosticsModuleProps> = ({ currentData, history }) => {
  const { cpuLoad, network } = currentData;
  const isGoodSignal = network.rssi > -60;
  const isFairSignal = network.rssi <= -60 && network.rssi > -80;

  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full">
      <h2 className="text-xl font-semibold text-slate-200 mb-4 flex items-center gap-2">
        <Activity className="h-5 w-5 text-indigo-400" />
        System Diagnostics
      </h2>
      
      <div className="space-y-6 flex-1 flex flex-col">
        {/* CPU Load Panel (Electric Green) */}
        <div className="bg-slate-900 rounded-lg p-5 border border-slate-700/30 flex-1 flex flex-col min-h-[220px]">
          <div className="flex justify-between items-center mb-3">
            <span className="text-sm font-medium text-slate-400 flex items-center gap-2">
              <Cpu className="w-4 h-4 text-emerald-400" />
              CPU Load
            </span>
            <span className="text-xl font-bold" style={{ color: '#10b981' }}>{cpuLoad}%</span>
          </div>
          
          {/* Chart Wrapper to give Task Manager look */}
          <div className="flex-1 w-full bg-[#0a0f1c] rounded-md overflow-hidden p-2 border border-emerald-900/30 relative">
             <RollingChart 
                dataHistory={history} 
                dataKeyExtractor={h => h.system.cpuLoad} 
                color="#10b981" 
                label="CPU Utilization" 
                unit="%" 
                type="step"
                domain={[0, 100]}
              />
          </div>
        </div>

        {/* Network Info */}
        <div className="bg-slate-900 rounded-lg p-5 border border-slate-700/30">
          <div className="flex justify-between items-center mb-4">
            <span className="text-sm font-medium text-slate-400 flex items-center gap-2">
              <Wifi className={`w-4 h-4 ${isGoodSignal ? 'text-green-400' : isFairSignal ? 'text-yellow-400' : 'text-red-400'}`} />
              Network (WiFi)
            </span>
            <div className="flex space-x-1 items-end h-4">
              <div className="w-1.5 h-1/4 bg-green-500 rounded-t-sm"></div>
              <div className="w-1.5 h-2/4 bg-green-500 rounded-t-sm"></div>
              <div className={`w-1.5 h-3/4 rounded-t-sm ${isFairSignal || isGoodSignal ? 'bg-green-500' : 'bg-slate-700'}`}></div>
              <div className={`w-1.5 h-full rounded-t-sm ${isGoodSignal ? 'bg-green-500' : 'bg-slate-700'}`}></div>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-4 text-sm">
            <div className="flex flex-col">
              <span className="text-slate-500 mb-1 text-xs uppercase tracking-wider">SSID</span>
              <span className="text-slate-300 font-medium">{network.ssid}</span>
            </div>
            <div className="flex flex-col col-span-2">
              <span className="text-slate-500 mb-1 text-xs uppercase tracking-wider">Signal Strength (RSSI)</span>
              <span className="text-slate-300 font-medium">{network.rssi} dBm</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

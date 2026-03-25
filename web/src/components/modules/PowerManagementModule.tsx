import React from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { Battery, Zap } from 'lucide-react';

interface PowerManagementModuleProps {
  currentData: TelemetryData['power'];
}

export const PowerManagementModule: React.FC<PowerManagementModuleProps> = ({ currentData }) => {
  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full">
      <h2 className="text-xl font-semibold text-slate-200 mb-4 flex items-center gap-2">
        <Zap className="text-green-400 h-5 w-5" />
        Power Management
      </h2>
      
      <div className="grid grid-cols-2 gap-4 flex-1">
        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center">
          <span className="text-sm text-slate-400 mb-1">Supply Voltage</span>
          <span className="text-2xl font-bold text-green-400">{currentData.voltage.toFixed(2)} V</span>
        </div>
        
        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center">
          <span className="text-sm text-slate-400 mb-1">Current Draw</span>
          <span className="text-2xl font-bold text-rose-400">{currentData.current.toFixed(1)} mA</span>
        </div>

        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center col-span-2 sm:col-span-1">
          <span className="text-sm text-slate-400 mb-1">Total Consumption</span>
          <span className="text-xl font-bold text-blue-400">{currentData.totalEnergy.toFixed(2)} mAh</span>
        </div>

        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center col-span-2 sm:col-span-1">
          <div className="flex items-center gap-2 mb-1">
            <Battery className="h-4 w-4 text-emerald-400" />
            <span className="text-sm text-slate-400">Est. Battery Life</span>
          </div>
          <span className="text-xl font-bold text-emerald-400">{currentData.batteryLifeStr}</span>
        </div>
      </div>
    </div>
  );
};

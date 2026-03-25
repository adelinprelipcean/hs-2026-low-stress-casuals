import React from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { Battery, BatteryWarning, Zap, PlugZap } from 'lucide-react';

interface PowerManagementModuleProps {
  currentData: TelemetryData['power'];
}

export const PowerManagementModule: React.FC<PowerManagementModuleProps> = ({ currentData }) => {
  // Dacă voltajul este aproape de 5V sau durata bateriei e invalidă, e clar pe sursă externă (USB)
  const isUsbPowered = currentData.voltage > 4.5 || currentData.batteryLifeStr === 'N/A';

  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full">
      <h2 className="text-xl font-semibold text-slate-200 mb-4 flex items-center gap-2">
        <Zap className="text-green-400 h-5 w-5" />
        Power Management
      </h2>
      
      <div className="grid grid-cols-2 gap-4 flex-1">
        {/* Acestea le păstrăm mereu vizibile pentru că citesc curentul din USB */}
        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center">
          <span className="text-sm text-slate-400 mb-1">Supply Voltage</span>
          <span className="text-2xl font-bold text-green-400">{currentData.voltage.toFixed(2)} V</span>
        </div>
        
        <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center">
          <span className="text-sm text-slate-400 mb-1">Current Draw</span>
          <span className="text-2xl font-bold text-rose-400">{currentData.current.toFixed(2)} mA</span>
        </div>

        {isUsbPowered ? (
          <div className="bg-slate-900/80 rounded-lg p-4 flex items-center justify-center col-span-2 gap-4 border border-indigo-500/30">
            <PlugZap className="h-6 w-6 text-indigo-400 animate-pulse" />
            <div className="text-left">
              <div className="text-slate-200 font-medium tracking-wide">USB / External Power</div>
              <div className="text-xs text-slate-400">Battery disconnected or fully charged</div>
            </div>
          </div>
        ) : (
          <>
            <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center col-span-2 sm:col-span-1">
              <span className="text-sm text-slate-400 mb-1">Total Consumption</span>
              <span className="text-xl font-bold text-blue-400">{currentData.totalEnergy.toFixed(2)} mAh</span>
            </div>

            <div className="bg-slate-900 rounded-lg p-4 flex flex-col justify-center col-span-2 sm:col-span-1">
              <div className="flex items-center gap-2 mb-1">
                <Battery className="h-4 w-4 text-emerald-400" />
                <span className="text-sm text-slate-400">Est. Battery Life</span>
              </div>
              <div className="flex items-baseline gap-2">
                <span className="text-xl font-bold text-emerald-400">{currentData.batteryLifeStr}</span>
                <span className="text-sm font-medium text-emerald-500/80">({currentData.batteryPercentage}%)</span>
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
};

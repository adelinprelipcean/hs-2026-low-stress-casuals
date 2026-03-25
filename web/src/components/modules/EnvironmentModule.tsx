import React from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { RollingChart } from '../common/RollingChart';

interface EnvironmentModuleProps {
  currentData: TelemetryData['environment'];
  history: TelemetryData[];
}

export const EnvironmentModule: React.FC<EnvironmentModuleProps> = ({ currentData, history }) => {
  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full">
      <h2 className="text-xl font-semibold text-slate-200 mb-4">Environment Sensors</h2>
      
      <div className="flex-1 flex flex-col gap-5">
        {/* Temperature Chart (Sunset Orange/Red) */}
        <div className="flex-1 bg-slate-900 rounded-lg p-5 border border-slate-700/30 flex flex-col min-h-[200px]">
          <div className="flex justify-between items-end mb-3">
            <span className="text-sm font-medium text-slate-400">Temperature</span>
            <span className="text-3xl font-bold" style={{ color: '#ea580c' }}>{currentData.temperature}°C</span>
          </div>
          <div className="flex-1 w-full">
            <RollingChart 
              dataHistory={history} 
              dataKeyExtractor={h => h.environment.temperature} 
              color="#ea580c" 
              label="Temperature" 
              unit="°C" 
              type="monotone"
              domain={['dataMin - 1', 'dataMax + 1']}
            />
          </div>
        </div>

        {/* Light Intensity Chart (Golden Yellow) */}
        <div className="flex-1 bg-slate-900 rounded-lg p-5 border border-slate-700/30 flex flex-col min-h-[200px]">
          <div className="flex justify-between items-end mb-3">
            <span className="text-sm font-medium text-slate-400">Light Intensity</span>
            <span className="text-3xl font-bold" style={{ color: '#eab308' }}>{currentData.lightIntensity} lx</span>
          </div>
          <div className="flex-1 w-full relative">
            <RollingChart 
              dataHistory={history} 
              dataKeyExtractor={h => h.environment.lightIntensity} 
              color="#eab308" 
              label="Light Intensity" 
              unit="lx"
              type="monotone"
              domain={['dataMin - 50', 'dataMax + 50']}
            />
          </div>
        </div>
      </div>
    </div>
  );
};

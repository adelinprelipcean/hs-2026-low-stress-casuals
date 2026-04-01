import React, { useMemo } from 'react';
import type { TelemetryData } from '../../types/telemetry';
import { RollingChart } from '../common/RollingChart';

// Environment Module
interface EnvironmentModuleProps {
  currentData: TelemetryData['environment'];
  history: TelemetryData[];
}

/**
 * Intoarce un gradient CSS bazat pe temperatura per sa creem acel "Ambient Gradient"
 * Folosim tranzitii fine (interpolare lerp manuala) pentru a nu "sări" de la o culoare la alta.
 */
function getTemperatureGradient(temp: number) {
  // Limitam temperatura intre extretele vizuale pentru a nu iesi din grafic
  const clampedTemp = Math.max(0, Math.min(35, temp));
  
  let r, g, b;

  if (clampedTemp <= 10) {
    // Sub 10: Albastru pur
    r = 14; g = 165; b = 233;
  } else if (clampedTemp > 10 && clampedTemp <= 22) {
    // Intre 10 si 22: Tranzitie de la Albastru -> Galben
    const t = (clampedTemp - 10) / (22 - 10); // 0 (la 10 grade) -> 1 (la 22 grade)
    r = Math.round(14 + (234 - 14) * t); // Sky Blue -> Amber
    g = Math.round(165 + (179 - 165) * t);
    b = Math.round(233 + (8 - 233) * t);
  } else {
    // Peste 22: Tranzitie de la Galben -> Rosu aprins
    const t = Math.min((clampedTemp - 22) / (35 - 22), 1); // 0 (la 22 grade) -> 1 (la 35 grade max)
    r = Math.round(234 + (239 - 234) * t); // Amber -> Red
    g = Math.round(179 + (68 - 179) * t);
    b = Math.round(8 + (68 - 8) * t);
  }

  return `linear-gradient(135deg, rgba(${r}, ${g}, ${b}, 0.35) 0%, rgba(15, 23, 42, 0.8) 100%)`;
}

function getTemperatureColor(temp: number) {
  const clampedTemp = Math.max(0, Math.min(35, temp));
  let r, g, b;

  if (clampedTemp <= 10) {
    r = 56; g = 189; b = 248; // sky-400
  } else if (clampedTemp > 10 && clampedTemp <= 22) {
    const t = (clampedTemp - 10) / (22 - 10);
    r = Math.round(56 + (250 - 56) * t);  // Sky to Yellow
    g = Math.round(189 + (204 - 189) * t);
    b = Math.round(248 + (21 - 248) * t);
  } else {
    const t = Math.min((clampedTemp - 22) / (35 - 22), 1);
    r = Math.round(250 + (248 - 250) * t); // Yellow to Red
    g = Math.round(204 + (113 - 204) * t);
    b = Math.round(21 + (113 - 21) * t);
  }

  return `rgb(${r}, ${g}, ${b})`;
}

// Afisare NTC temperatura cu gradient (Senzorul LDR a fost scos conform Task 2)
export const EnvironmentModule: React.FC<EnvironmentModuleProps> = ({ currentData, history }) => {
  const bgGradient = useMemo(() => getTemperatureGradient(currentData.temperature), [currentData.temperature]);
  const mainColor = useMemo(() => getTemperatureColor(currentData.temperature), [currentData.temperature]);

  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 flex flex-col h-full relative overflow-hidden transition-all duration-1000 ease-in-out" style={{ background: bgGradient }}>
      <h2 className="text-xl font-semibold text-slate-200 mb-4 z-10 relative">Environment Sensor (NTC)</h2>
      
      <div className="flex-1 flex flex-col gap-5 z-10 relative">
        {/* Temperature Chart Full Height */}
        <div className="flex-1 bg-slate-900/60 backdrop-blur-md rounded-lg p-5 border border-slate-700/50 flex flex-col min-h-[400px]">
          <div className="flex justify-between items-end mb-3">
             <div>
              <span className="text-sm font-bold tracking-wider text-slate-400 uppercase">Ambient Temperature</span>
              <p className="text-xs text-slate-500 mt-1">Thermistor 100k</p>
             </div>
            <span className="text-5xl font-bold transition-colors duration-500" style={{ color: mainColor }}>{currentData.temperature}°C</span>
          </div>
          <div className="flex-1 w-full mt-4">
            <RollingChart 
              dataHistory={history} 
              dataKeyExtractor={h => h.environment.temperature} 
              color={mainColor} 
              label="Temperature" 
              unit="°C" 
              type="monotone"
              domain={['dataMin - 2', 'dataMax + 2']}
            />
          </div>
        </div>
      </div>
    </div>
  );
};

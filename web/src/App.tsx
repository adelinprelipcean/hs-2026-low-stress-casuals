import { useEsp32Telemetry } from './hooks/useEsp32Telemetry';
import { EnvironmentModule } from './components/modules/EnvironmentModule';
import { PowerManagementModule } from './components/modules/PowerManagementModule';
import { SystemDiagnosticsModule } from './components/modules/SystemDiagnosticsModule';
import { IoLogModule } from './components/modules/IoLogModule';
import { Imu3DModule } from './components/modules/Imu3DModule';
import { Cpu, WifiOff, Globe } from 'lucide-react';
import './index.css';

function App() {
  const { currentData, dataHistory, logs, dataSource, sourceMode, setSourceMode } = useEsp32Telemetry();

  // Daca nu avem date INCA aratam loading screen-ul, DAR adaugam selectorul de Mock Data chiar si aici!
  if (!currentData) {
    return (
      <div className="min-h-screen bg-slate-950 flex flex-col items-center justify-center text-slate-400 relative">
        {/* Selectorul ascuns sus dreapta ca sa poti trisa cand e offline */}
        <div className="absolute top-6 right-6">
          <label className="flex items-center gap-2 text-xs text-slate-400">
            <span className="uppercase tracking-wide">Mode</span>
            <select
              value={sourceMode}
              onChange={(e) => setSourceMode(e.target.value as 'auto' | 'force-esp' | 'force-mock')}
              className="bg-slate-900 border border-slate-700 text-slate-200 rounded-md px-2 py-1 focus:outline-none focus:ring-2 focus:ring-cyan-500/60"
            >
              <option value="auto">Auto</option>
              <option value="force-esp">Force ESP</option>
              <option value="force-mock">Force Mock</option>
            </select>
          </label>
        </div>

        <Cpu className="w-16 h-16 mb-4 animate-pulse text-indigo-500" />
        <h1 className="text-2xl font-semibold text-slate-200 tracking-wide">Initializing Telemetry...</h1>
        <p className="mt-2 text-sm text-center">Polling ESP32-C3 Super Mini telemetry<br/><span className="text-xs text-slate-500 block mt-2">(Select 'Force Mock' top right to test without hardware)</span></p>
      </div>
    );
  }

  const isOffline = !currentData.connected;

  return (
    <div className="min-h-screen bg-slate-950 text-slate-200 font-sans p-6 pb-12">
      {/* Header */}
      <header className="max-w-7xl mx-auto mb-8 flex justify-between items-center">
        <div>
          <h1 className="text-3xl font-bold bg-gradient-to-r from-blue-400 to-emerald-400 text-transparent bg-clip-text tracking-tight">
            ESP32 IoT Dashboard
          </h1>
          <p className="text-sm text-slate-400 mt-1">Real-time C3 Super Mini Diagnostics</p>
        </div>

        <div className="flex items-center gap-4">
          <label className="flex items-center gap-2 text-xs text-slate-400">
            <span className="uppercase tracking-wide">Mode</span>
            <select
              value={sourceMode}
              onChange={(e) => setSourceMode(e.target.value as 'auto' | 'force-esp' | 'force-mock')}
              className="bg-slate-900 border border-slate-700 text-slate-200 rounded-md px-2 py-1 focus:outline-none focus:ring-2 focus:ring-cyan-500/60"
            >
              <option value="auto">Auto</option>
              <option value="force-esp">Force ESP</option>
              <option value="force-mock">Force Mock</option>
            </select>
          </label>

          <div
            className={`flex items-center gap-2 px-3 py-1.5 rounded-full border text-xs font-semibold tracking-wide ${
              dataSource === 'esp'
                ? 'bg-cyan-500/10 border-cyan-500/30 text-cyan-300'
                : dataSource === 'mock'
                ? 'bg-amber-500/10 border-amber-500/30 text-amber-300'
                : 'bg-slate-500/10 border-slate-500/30 text-slate-300'
            }`}
          >
            <span className="h-2 w-2 rounded-full bg-current opacity-90" />
            <span>
              {dataSource === 'esp' ? 'SOURCE: ESP32 LIVE' : dataSource === 'mock' ? 'SOURCE: MOCK DATA' : 'SOURCE: DETECTING'}
            </span>
          </div>

          {/* Offline Indicator */}
          {isOffline ? (
            <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-red-500/10 border border-red-500/20 text-red-400">
              <WifiOff className="h-4 w-4" />
              <span className="text-sm font-semibold tracking-wide">OFFLINE</span>
            </div>
          ) : (
             <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-emerald-500/10 border border-emerald-500/20 text-emerald-400">
             <Globe className="h-4 w-4" />
             <span className="text-sm font-semibold tracking-wide">ONLINE</span>
           </div>
          )}
        </div>
      </header>

      {/* Main Grid Layout */}
      <main className="max-w-7xl mx-auto grid grid-cols-1 lg:grid-cols-12 gap-6 relative">
        {/* Connection overlay if offline */}
        {isOffline && (
          <div className="absolute inset-0 z-50 bg-slate-950/60 backdrop-blur-sm rounded-xl flex items-center justify-center border border-red-500/20">
             <div className="bg-slate-900 p-6 rounded-xl border border-red-500/30 shadow-2xl flex flex-col items-center">
                <WifiOff className="h-10 w-10 text-red-400 mb-3" />
                <h2 className="text-xl font-bold text-slate-200">Device Offline</h2>
                 <p className="text-slate-400 text-sm mt-2">Waiting for telemetry GET response...</p>
             </div>
          </div>
        )}

        {/* Top/Left Section - Environment (Span 8) */}
        <div className="lg:col-span-8 h-full min-h-[400px]">
          <EnvironmentModule currentData={currentData.environment} history={dataHistory} />
        </div>

        {/* Top/Right Section - System Config (Span 4) */}
        <div className="lg:col-span-4 h-full">
          <SystemDiagnosticsModule currentData={currentData.system} history={dataHistory} />
        </div>

        {/* 3D Visualizer Row (Span 12) */}
        {currentData.imu && (
          <div className="lg:col-span-12 h-full">
            <Imu3DModule currentData={currentData.imu} />
          </div>
        )}

        {/* Bottom/Left Section - Power Management (Span 5) */}
        <div className="lg:col-span-5 h-full">
          <PowerManagementModule currentData={currentData.power} />
        </div>

        {/* Bottom/Right Section - Terminal (Span 7) */}
        <div className="lg:col-span-7 h-full">
          <IoLogModule logs={logs} />
        </div>
      </main>
    </div>
  );
}

export default App;

import { useEffect, useState } from 'react';
import { useEsp32Telemetry } from './hooks/useEsp32Telemetry';
import { EnvironmentModule } from './components/modules/EnvironmentModule';
import { PowerManagementModule } from './components/modules/PowerManagementModule';
import { SystemDiagnosticsModule } from './components/modules/SystemDiagnosticsModule';
import { IoLogModule } from './components/modules/IoLogModule';
import { Imu3DModule } from './components/modules/Imu3DModule';
import { ModuleStatusModule } from './components/modules/ModuleStatusModule';
import { Cpu, WifiOff, Globe, Moon, Sun } from 'lucide-react';
import './index.css';

function App() {
  const { currentData, dataHistory, logs, dataSource, sourceMode, setSourceMode } = useEsp32Telemetry();
  const [theme, setTheme] = useState<'dark' | 'light'>(() => {
    if (typeof window === 'undefined') return 'dark';
    const storedTheme = window.localStorage.getItem('dashboard-theme');
    if (storedTheme === 'light' || storedTheme === 'dark') return storedTheme;
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  });

  useEffect(() => {
    document.body.classList.remove('theme-light', 'theme-dark');
    document.body.classList.add(theme === 'light' ? 'theme-light' : 'theme-dark');
    window.localStorage.setItem('dashboard-theme', theme);
  }, [theme]);

  // Daca nu avem date INCA aratam loading screen-ul, DAR adaugam selectorul de Mock Data chiar si aici!
  if (!currentData) {
    return (
      <div className={`min-h-screen flex flex-col items-center justify-center relative transition-colors ${theme === 'light' ? 'theme-light bg-slate-100 text-slate-500' : 'theme-dark bg-slate-950 text-slate-400'}`}>
        {/* Selectorul ascuns sus dreapta ca sa poti trisa cand e offline */}
        <div className="absolute top-6 right-6 flex items-center gap-3">
          <button
            type="button"
            onClick={() => setTheme((prev) => (prev === 'dark' ? 'light' : 'dark'))}
            className="flex items-center gap-2 rounded-md border border-slate-700 bg-slate-900 px-2 py-1 text-xs text-slate-200 transition hover:bg-slate-800"
            aria-label="Toggle theme"
            title={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
          >
            {theme === 'dark' ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
            <span>{theme === 'dark' ? 'Light' : 'Dark'}</span>
          </button>

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
  const hasPinsReport = currentData.moduleHealth.hasPinsReport;
  const moduleStateFromHealth = (isConnected: boolean) =>
    (hasPinsReport ? isConnected : !isOffline) ? ('ok' as const) : ('error' as const);

  // Frontend-only heuristics for modules that do not expose dedicated status bits yet.
  const oledLikelyConnected =
    !isOffline &&
    (
      hasPinsReport
        ? currentData.moduleHealth.rtcConnected || currentData.moduleHealth.gyroscopeConnected
        : true
    );

  const chargerTelemetryLooksValid =
    Number.isFinite(currentData.power.voltage) &&
    Number.isFinite(currentData.power.current) &&
    Number.isFinite(currentData.power.batteryPercentage) &&
    currentData.power.voltage >= 2.5 &&
    currentData.power.voltage <= 5.5 &&
    currentData.power.batteryPercentage >= 0 &&
    currentData.power.batteryPercentage <= 100;

  const chargerLikelyConnected = !isOffline && (moduleStateFromHealth(currentData.moduleHealth.ina219Connected) === 'ok' || chargerTelemetryLooksValid);

  const moduleStatusItems = [
    {
      id: 'imu',
      label: 'BMI160 IMU',
      status: moduleStateFromHealth(currentData.moduleHealth.gyroscopeConnected),
      subtitle: '3D orientation stream',
    },
    {
      id: 'ntc',
      label: 'NTC Temperature',
      status: moduleStateFromHealth(currentData.moduleHealth.thermistorConnected),
      subtitle: 'Environment sensor',
    },
    {
      id: 'power',
      label: 'Power Monitor',
      status: moduleStateFromHealth(currentData.moduleHealth.ina219Connected),
      subtitle: 'Voltage and current',
    },
    {
      id: 'charger',
      label: 'HW-168 Charger',
      status: chargerLikelyConnected ? ('ok' as const) : ('error' as const),
      subtitle: 'Heuristic from power telemetry',
    },
    {
      id: 'rtc',
      label: 'RTC Clock',
      status: moduleStateFromHealth(currentData.moduleHealth.rtcConnected),
      subtitle: 'Real-time clock source',
    },
    {
      id: 'oled',
      label: 'OLED 0.96"',
      status: oledLikelyConnected ? ('ok' as const) : ('error' as const),
      subtitle: 'Heuristic from I2C module health',
    },
    { id: 'ws', label: 'WebSocket Link', status: isOffline ? 'error' as const : 'ok' as const, subtitle: 'Frontend transport' },
    { id: 'logic', label: 'Logic Analyzer', status: isOffline ? 'error' as const : 'ok' as const, subtitle: 'I2C capture pipeline' },
    {
      id: 'system',
      label: 'System Health',
      status: isOffline ? ('error' as const) : ('ok' as const),
      subtitle: 'CPU and network stats',
    },
  ];

  const pinStatusItems = [
    { id: 'gpio5', status: currentData.pins.gpio5 },
    { id: 'gpio6', status: currentData.pins.gpio6 },
    { id: 'gpio7', status: currentData.pins.gpio7 },
    { id: 'gpio8', status: currentData.pins.gpio8 },
    { id: 'gpio9', status: currentData.pins.gpio9 },
    { id: 'gpio10', status: currentData.pins.gpio10 },
    { id: 'gpio20', status: currentData.pins.gpio20 },
    { id: 'gpio21', status: currentData.pins.gpio21 },
    { id: '5v', status: currentData.pins.p5v },
    { id: 'gnd', status: currentData.pins.gnd },
    { id: '3v3', status: currentData.pins.p3v3 },
    { id: 'gpio4', status: currentData.pins.gpio4 },
    { id: 'gpio3', status: currentData.pins.gpio3 },
    { id: 'gpio2', status: currentData.pins.gpio2 },
    { id: 'gpio1', status: currentData.pins.gpio1 },
    { id: 'gpio0', status: currentData.pins.gpio0 },
  ];

  return (
    <div className={`min-h-screen font-sans p-6 pb-12 transition-colors ${theme === 'light' ? 'theme-light bg-slate-100 text-slate-900' : 'theme-dark bg-slate-950 text-slate-200'}`}>
      {/* Header */}
      <header className="max-w-7xl mx-auto mb-8 flex justify-between items-center">
        <div>
          <h1 className="text-3xl font-bold bg-gradient-to-r from-blue-400 to-emerald-400 text-transparent bg-clip-text tracking-tight">
            ESP32 IoT Dashboard
          </h1>
          <p className="text-sm text-slate-400 mt-1">Real-time C3 Super Mini Diagnostics</p>
        </div>

        <div className="flex items-center gap-4">
          <button
            type="button"
            onClick={() => setTheme((prev) => (prev === 'dark' ? 'light' : 'dark'))}
            className="flex items-center gap-2 rounded-md border border-slate-700 bg-slate-900 px-2.5 py-1.5 text-xs text-slate-200 transition hover:bg-slate-800"
            aria-label="Toggle theme"
            title={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
          >
            {theme === 'dark' ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
            <span className="uppercase tracking-wide">{theme === 'dark' ? 'Light' : 'Dark'}</span>
          </button>

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

        {/* Module Debug Status Panel */}
        <div className="lg:col-span-12 h-full">
          <ModuleStatusModule modules={moduleStatusItems} pins={pinStatusItems} />
        </div>

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

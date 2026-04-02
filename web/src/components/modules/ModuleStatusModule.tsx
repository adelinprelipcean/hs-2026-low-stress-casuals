import React from 'react';
import { ShieldCheck } from 'lucide-react';

export interface ModuleStatusItem {
  id: string;
  label: string;
  status: 'ok' | 'error';
  subtitle?: string;
}

export interface PinStatusItem {
  id: string;
  status: 'ok' | 'error' | 'unknown';
}

interface ModuleStatusModuleProps {
  modules: ModuleStatusItem[];
  pins?: PinStatusItem[];
}

type PinLabelTone = 'gpio' | 'power' | 'ground';

interface PinLayoutRow {
  id: string;
  side: 'left' | 'right';
  row: number;
  label: string;
  tone: PinLabelTone;
}

const PIN_LAYOUT: PinLayoutRow[] = [
  { id: 'gpio5', side: 'left', row: 0, label: 'GPIO5', tone: 'gpio' },
  { id: 'gpio6', side: 'left', row: 1, label: 'GPIO6', tone: 'gpio' },
  { id: 'gpio7', side: 'left', row: 2, label: 'GPIO7', tone: 'gpio' },
  { id: 'gpio8', side: 'left', row: 3, label: 'GPIO8', tone: 'gpio' },
  { id: 'gpio9', side: 'left', row: 4, label: 'GPIO9', tone: 'gpio' },
  { id: 'gpio10', side: 'left', row: 5, label: 'GPIO10', tone: 'gpio' },
  { id: 'gpio20', side: 'left', row: 6, label: 'GPIO20', tone: 'gpio' },
  { id: 'gpio21', side: 'left', row: 7, label: 'GPIO21', tone: 'gpio' },
  { id: '5v', side: 'right', row: 0, label: '5V', tone: 'power' },
  { id: 'gnd', side: 'right', row: 1, label: 'GND', tone: 'ground' },
  { id: '3v3', side: 'right', row: 2, label: '3.3V', tone: 'power' },
  { id: 'gpio4', side: 'right', row: 3, label: 'GPIO4', tone: 'gpio' },
  { id: 'gpio3', side: 'right', row: 4, label: 'GPIO3', tone: 'gpio' },
  { id: 'gpio2', side: 'right', row: 5, label: 'GPIO2', tone: 'gpio' },
  { id: 'gpio1', side: 'right', row: 6, label: 'GPIO1', tone: 'gpio' },
  { id: 'gpio0', side: 'right', row: 7, label: 'GPIO0', tone: 'gpio' },
];

// Calibrated against the board image so dots sit over pin holes.
const LEFT_PIN_TOP_BY_ROW_PERCENT = [18.2, 27.2, 36.0, 45.0, 54.0, 63.2, 72.4, 81.2] as const;
const RIGHT_PIN_TOP_BY_ROW_PERCENT = [18.2, 27.2, 36.0, 45.0, 54.0, 63.2, 72.4, 81.2] as const;
const PIN_HOLE_X_OFFSET_LEFT_PX = 88;
const PIN_HOLE_X_OFFSET_RIGHT_PX = 84;
const pinTopPercent = (side: 'left' | 'right', row: number) => {
  const table = side === 'left' ? LEFT_PIN_TOP_BY_ROW_PERCENT : RIGHT_PIN_TOP_BY_ROW_PERCENT;
  return table[row] ?? table[0];
};

const pinLabelClass = (tone: PinLabelTone) => {
  switch (tone) {
    case 'gpio':
      return 'bg-lime-600 text-white';
    case 'power':
      return 'bg-red-600 text-white';
    case 'ground':
      return 'bg-black text-white';
    default:
      return 'bg-slate-600 text-white';
  }
};

const pinDotClass = (status: PinStatusItem['status']) => {
  if (status === 'ok') {
    return 'bg-emerald-400 shadow-[0_0_12px_rgba(52,211,153,0.8)]';
  }
  if (status === 'error') {
    return 'bg-rose-400 shadow-[0_0_12px_rgba(251,113,133,0.8)]';
  }
  return 'bg-slate-500 shadow-[0_0_8px_rgba(100,116,139,0.5)]';
};

export const ModuleStatusModule: React.FC<ModuleStatusModuleProps> = ({ modules, pins }) => {
  const statusByPin = React.useMemo(() => {
    const statusMap = new Map<string, PinStatusItem['status']>();
    (pins ?? []).forEach((pin) => {
      statusMap.set(pin.id.toLowerCase(), pin.status);
    });
    return statusMap;
  }, [pins]);

  const pinRows = React.useMemo(
    () => PIN_LAYOUT.map((pin) => ({ ...pin, status: statusByPin.get(pin.id.toLowerCase()) ?? 'unknown' as const })),
    [statusByPin],
  );

  const leftPins = pinRows.filter((pin) => pin.side === 'left');
  const rightPins = pinRows.filter((pin) => pin.side === 'right');

  return (
    <div className="bg-slate-800 rounded-xl p-6 shadow-lg border border-slate-700/50 h-full">
      <h2 className="text-xl font-semibold text-slate-200 mb-4 flex items-center gap-2">
        <ShieldCheck className="h-5 w-5 text-cyan-400" />
        Module Status
      </h2>

      <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-3">
        {modules.map((module) => {
          const isOk = module.status === 'ok';

          return (
            <div
              key={module.id}
              className="rounded-lg border border-slate-700/50 bg-slate-900/70 px-4 py-3 flex items-center justify-between"
            >
              <div>
                <div className="text-sm font-semibold text-slate-200">{module.label}</div>
                <div className="text-xs text-slate-500 mt-0.5">{module.subtitle ?? 'Status monitor'}</div>
              </div>

              <div className="flex items-center gap-3">
                <div className="flex items-center gap-1.5" title="OK">
                  <span
                    className={`h-3 w-3 rounded-full transition-all duration-200 ${
                      isOk
                        ? 'bg-emerald-400 shadow-[0_0_12px_rgba(52,211,153,0.75)]'
                        : 'bg-emerald-500/20'
                    }`}
                  />
                  <span className="text-[10px] uppercase tracking-wide text-slate-400">On</span>
                </div>

                <div className="flex items-center gap-1.5" title="Error">
                  <span
                    className={`h-3 w-3 rounded-full transition-all duration-200 ${
                      !isOk
                        ? 'bg-rose-400 shadow-[0_0_12px_rgba(251,113,133,0.75)]'
                        : 'bg-rose-500/20'
                    }`}
                  />
                  <span className="text-[10px] uppercase tracking-wide text-slate-400">Off</span>
                </div>
              </div>
            </div>
          );
        })}
      </div>

      <div className="mt-6 rounded-xl border border-slate-700/60 bg-slate-900/60 p-4">
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-sm font-semibold tracking-wide uppercase text-slate-300">ESP32-C3 Pin State Map</h3>
          <span className="text-[11px] text-slate-500">Live status from header 0xC1</span>
        </div>

        <div className="flex justify-center overflow-x-auto">
          <div className="relative min-w-[620px] py-3 px-10 sm:px-16 overflow-visible">
            <img
              src="/esp32_c3_mini.png"
              alt="ESP32-C3 Super Mini pin layout"
              className="mx-auto h-auto w-[220px] sm:w-[240px] object-contain drop-shadow-[0_10px_24px_rgba(15,23,42,0.55)]"
            />

            {leftPins.map((pin) => (
              <div
                key={pin.id}
                className="absolute -translate-x-1/2 -translate-y-1/2"
                style={{
                  top: `${pinTopPercent(pin.side, pin.row)}%`,
                  left: `calc(50% - ${PIN_HOLE_X_OFFSET_LEFT_PX}px)`,
                }}
              >
                <span
                  className={`relative z-10 block h-4 w-4 rounded-full border border-slate-900/70 ${pinDotClass(pin.status)}`}
                />
                <div className="absolute right-[10px] top-1/2 -translate-y-1/2 flex items-center gap-1">
                  <span
                    className={`whitespace-nowrap rounded-sm px-2.5 py-[2px] text-[11px] font-semibold tracking-wide ${pinLabelClass(pin.tone)}`}
                  >
                    {pin.label}
                  </span>
                  <span className="h-px w-4 bg-slate-500/70" />
                </div>
              </div>
            ))}

            {rightPins.map((pin) => (
              <div
                key={pin.id}
                className="absolute -translate-x-1/2 -translate-y-1/2"
                style={{
                  top: `${pinTopPercent(pin.side, pin.row)}%`,
                  left: `calc(50% + ${PIN_HOLE_X_OFFSET_RIGHT_PX}px)`,
                }}
              >
                <span
                  className={`relative z-10 block h-4 w-4 rounded-full border border-slate-900/70 ${pinDotClass(pin.status)}`}
                />
                <div className="absolute left-[10px] top-1/2 -translate-y-1/2 flex items-center gap-1">
                  <span className="h-px w-4 bg-slate-500/70" />
                  <span
                    className={`whitespace-nowrap rounded-sm px-2.5 py-[2px] text-[11px] font-semibold tracking-wide ${pinLabelClass(pin.tone)}`}
                  >
                    {pin.label}
                  </span>
                </div>
              </div>
            ))}
          </div>
        </div>

        <div className="mt-3 flex items-center justify-center gap-4 text-[11px] text-slate-400">
          <span className="inline-flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-full bg-emerald-400" /> HIGH / OK</span>
          <span className="inline-flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-full bg-rose-400" /> LOW / FAIL</span>
          <span className="inline-flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-full bg-slate-500" /> UNKNOWN</span>
        </div>
      </div>
    </div>
  );
};
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

const PIN_ROWS = 8;

type PinLabelTone = 'gpio' | 'analog' | 'digital' | 'i2c' | 'spi' | 'uart' | 'power' | 'ground' | 'adc';

interface PinLabelTag {
  text: string;
  tone: PinLabelTone;
}

interface PinLayoutRow {
  id: string;
  side: 'left' | 'right';
  row: number;
  labels: PinLabelTag[];
}

const PIN_LAYOUT: PinLayoutRow[] = [
  { id: 'gpio5', side: 'left', row: 0, labels: [{ text: 'GPIO5', tone: 'gpio' }, { text: 'A3', tone: 'analog' }, { text: 'D3', tone: 'digital' }] },
  { id: 'gpio6', side: 'left', row: 1, labels: [{ text: 'GPIO6', tone: 'gpio' }, { text: 'SDA', tone: 'i2c' }, { text: 'D4', tone: 'digital' }] },
  { id: 'gpio7', side: 'left', row: 2, labels: [{ text: 'GPIO7', tone: 'gpio' }, { text: 'SCL', tone: 'i2c' }, { text: 'D5', tone: 'digital' }] },
  { id: 'gpio8', side: 'left', row: 3, labels: [{ text: 'GPIO8', tone: 'gpio' }, { text: 'SCK', tone: 'spi' }, { text: 'D8', tone: 'digital' }] },
  { id: 'gpio9', side: 'left', row: 4, labels: [{ text: 'GPIO9', tone: 'gpio' }, { text: 'MISO', tone: 'spi' }, { text: 'D9', tone: 'digital' }] },
  { id: 'gpio10', side: 'left', row: 5, labels: [{ text: 'GPIO10', tone: 'gpio' }, { text: 'MOSI', tone: 'spi' }, { text: 'D10', tone: 'digital' }] },
  { id: 'gpio20', side: 'left', row: 6, labels: [{ text: 'GPIO20', tone: 'gpio' }, { text: 'RX', tone: 'uart' }, { text: 'D7', tone: 'digital' }] },
  { id: 'gpio21', side: 'left', row: 7, labels: [{ text: 'GPIO21', tone: 'gpio' }, { text: 'TX', tone: 'uart' }, { text: 'D6', tone: 'digital' }] },
  { id: '5v', side: 'right', row: 0, labels: [{ text: '5V', tone: 'power' }] },
  { id: 'gnd', side: 'right', row: 1, labels: [{ text: 'GND', tone: 'ground' }] },
  { id: '3v3', side: 'right', row: 2, labels: [{ text: '3V3', tone: 'power' }] },
  { id: 'gpio4', side: 'right', row: 3, labels: [{ text: 'D2', tone: 'digital' }, { text: 'A2', tone: 'analog' }, { text: 'GPIO4', tone: 'gpio' }] },
  { id: 'gpio3', side: 'right', row: 4, labels: [{ text: 'D1', tone: 'digital' }, { text: 'A1', tone: 'analog' }, { text: 'GPIO3', tone: 'gpio' }] },
  { id: 'gpio2', side: 'right', row: 5, labels: [{ text: 'D0', tone: 'digital' }, { text: 'A0', tone: 'analog' }, { text: 'GPIO2', tone: 'gpio' }] },
  { id: 'gpio1', side: 'right', row: 6, labels: [{ text: 'ADC1-1', tone: 'adc' }, { text: 'GPIO1', tone: 'gpio' }] },
  { id: 'gpio0', side: 'right', row: 7, labels: [{ text: 'ADC1-0', tone: 'adc' }, { text: 'GPIO0', tone: 'gpio' }] },
];

const pinTopPercent = (row: number) => 12 + (row * (74 / (PIN_ROWS - 1)));

const pinLabelClass = (tone: PinLabelTone) => {
  switch (tone) {
    case 'gpio':
      return 'bg-lime-600 text-white';
    case 'analog':
      return 'bg-orange-300 text-white';
    case 'digital':
      return 'bg-blue-500 text-white';
    case 'i2c':
      return 'bg-emerald-500 text-white';
    case 'spi':
      return 'bg-indigo-500 text-white';
    case 'uart':
      return 'bg-slate-500 text-white';
    case 'power':
      return 'bg-red-600 text-white';
    case 'ground':
      return 'bg-black text-white';
    case 'adc':
      return 'bg-green-800 text-white';
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
          <span className="text-[11px] text-slate-500">UI preview: awaiting header 0xC1 mapping</span>
        </div>

        <div className="flex justify-center overflow-x-auto">
          <div className="relative min-w-[760px] py-3 px-28 sm:px-36 overflow-visible">
            <img
              src="/esp32_c3_mini.png"
              alt="ESP32-C3 Super Mini pin layout"
              className="mx-auto h-auto w-[220px] sm:w-[250px] object-contain drop-shadow-[0_10px_24px_rgba(15,23,42,0.55)]"
            />

            {leftPins.map((pin) => (
              <div
                key={pin.id}
                className="absolute left-[182px] -translate-x-full -translate-y-1/2 flex items-center gap-2"
                style={{ top: `${pinTopPercent(pin.row)}%` }}
              >
                <span className={`h-2.5 w-2.5 rounded-full ${pinDotClass(pin.status)}`} />
                <div className="flex items-center gap-1.5 whitespace-nowrap">
                  {pin.labels.map((label) => (
                    <span
                      key={`${pin.id}-${label.text}`}
                      className={`rounded-sm px-2 py-[2px] text-[11px] font-semibold tracking-wide ${pinLabelClass(label.tone)}`}
                    >
                      {label.text}
                    </span>
                  ))}
                </div>
              </div>
            ))}

            {rightPins.map((pin) => (
              <div
                key={pin.id}
                className="absolute right-[182px] translate-x-full -translate-y-1/2 flex items-center gap-2"
                style={{ top: `${pinTopPercent(pin.row)}%` }}
              >
                <span className={`h-2.5 w-2.5 rounded-full ${pinDotClass(pin.status)}`} />
                <div className="flex items-center gap-1.5 whitespace-nowrap">
                  {pin.labels.map((label) => (
                    <span
                      key={`${pin.id}-${label.text}`}
                      className={`rounded-sm px-2 py-[2px] text-[11px] font-semibold tracking-wide ${pinLabelClass(label.tone)}`}
                    >
                      {label.text}
                    </span>
                  ))}
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
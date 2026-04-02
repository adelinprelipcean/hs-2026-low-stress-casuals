import { useState, useEffect, useRef } from 'react';
import type { TelemetryData, IoLogEntry, Esp32Payload } from '../types/telemetry';
import { SmaSmoother } from '../lib/smoothing';

// Setam IP-ul local al ESP32-ului (In mod Access Point, default este 192.168.4.1)
// Portul 3333 este comun pentru librariile de WebSockets pe C++/ESP32
const WS_URL = "ws://192.168.4.1:3333";

const createDefaultTelemetryState = (): TelemetryData => ({
  timestamp: Date.now(),
  connected: true,
  environment: { temperature: 25.0 },
  pins: {
    gpio5: 'unknown',
    gpio6: 'unknown',
    gpio7: 'unknown',
    gpio8: 'unknown',
    gpio9: 'unknown',
    gpio10: 'unknown',
    gpio20: 'unknown',
    gpio21: 'unknown',
    gpio4: 'unknown',
    gpio3: 'unknown',
    gpio2: 'unknown',
    gpio1: 'unknown',
    gpio0: 'unknown',
    p5v: 'unknown',
    gnd: 'unknown',
    p3v3: 'unknown',
  },
  moduleHealth: {
    hasPinsReport: false,
    thermistorConnected: false,
    rtcConnected: false,
    gyroscopeConnected: false,
    ina219Connected: false,
  },
  imu: { pitch: 0, roll: 0, yaw: 0, accel: 1.0 },
  power: {
    voltage: 4.1,
    current: 0,
    batteryPercentage: 100,
    totalEnergy: 0,
    batteryLifeStr: '99h',
  },
  system: {
    cpuLoad: 15,
    network: { ssid: 'HS_IMU_STREAM', rssi: -50 },
  },
});

export function useEsp32Telemetry() {
  const [dataHistory, setDataHistory] = useState<TelemetryData[]>([]);
  const [currentData, setCurrentData] = useState<TelemetryData | null>(null);
  const [logs, setLogs] = useState<IoLogEntry[]>([]);
  
  // Modul sursei si detectia automata
  const [sourceMode, setSourceMode] = useState<'auto' | 'force-esp' | 'force-mock'>('auto');
  const [dataSource, setDataSource] = useState<'detecting' | 'esp' | 'mock'>('esp');

  const tempSmoother = useRef(new SmaSmoother(5));
  const watchdogRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mockIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const lastLogTimeRef = useRef<number>(0);
  const throttledLogRef = useRef<Record<string, number>>({});
  const lastImuSequenceRef = useRef<number | null>(null);
  const streamHealthRef = useRef<
    Record<'A1' | 'D4' | 'E1' | 'C1', { lastSeen: number | null; state: 'never' | 'live' | 'stale' }>
  >({
    A1: { lastSeen: null, state: 'never' },
    D4: { lastSeen: null, state: 'never' },
    E1: { lastSeen: null, state: 'never' },
    C1: { lastSeen: null, state: 'never' },
  });
  const lastSampleMicrosRef = useRef<number | null>(null);
  const telemetryStateRef = useRef<TelemetryData>(createDefaultTelemetryState());
  const orientationDegRef = useRef<{ x: number; y: number; z: number }>({
    x: 0,
    y: 0,
    z: 0,
  });

  const normalizeAngleDeg = (angle: number) => {
    let normalized = angle % 360;
    if (normalized > 180) normalized -= 360;
    if (normalized < -180) normalized += 360;
    return normalized;
  };

  const degToRad = (degrees: number) => (degrees * Math.PI) / 180;

  const binaryToPinStatus = (value: number): TelemetryData['pins']['gpio0'] =>
    value ? 'ok' : 'error';

  const commitTelemetryState = (nextData: TelemetryData, appendToHistory = false) => {
    telemetryStateRef.current = nextData;
    setCurrentData(nextData);
    if (appendToHistory) {
      setDataHistory((prev) => [...prev, nextData].slice(-60));
    }
  };

  // Funcție standardizata de generare loguri
  const generateLog = (msg: string, type: IoLogEntry['type'] = 'INFO') => {
    const entry: IoLogEntry = {
      id: Math.random().toString(36).substring(7),
      timestamp: Date.now(),
      type,
      message: msg
    };
    setLogs((prev) => [entry, ...prev].slice(0, 100));
  };

  const generateThrottledLog = (
    key: string,
    msg: string,
    type: IoLogEntry['type'] = 'WARN',
    cooldownMs = 3000
  ) => {
    const now = Date.now();
    const lastLoggedAt = throttledLogRef.current[key] ?? 0;
    if (now - lastLoggedAt < cooldownMs) return;

    throttledLogRef.current[key] = now;
    generateLog(msg, type);
  };

  // websocket connection and payload handling
  useEffect(() => {
    let ws: WebSocket | null = null;
    let reconnectTimeout: ReturnType<typeof setTimeout>;
    let streamMonitorInterval: ReturnType<typeof setInterval> | null = null;

    lastSampleMicrosRef.current = null;
    lastImuSequenceRef.current = null;
    orientationDegRef.current = { x: 0, y: 0, z: 0 };
    streamHealthRef.current = {
      A1: { lastSeen: null, state: 'never' },
      D4: { lastSeen: null, state: 'never' },
      E1: { lastSeen: null, state: 'never' },
      C1: { lastSeen: null, state: 'never' },
    };

    const streamStaleTimeoutMs: Record<'A1' | 'D4' | 'E1' | 'C1', number> = {
      A1: 1500,
      E1: 1500,
      D4: 4000,
      C1: 4000,
    };

    const markDataRestoredIfNeeded = () => {
      if (!telemetryStateRef.current.connected) {
        generateLog('Data stream restored: packets are being received again.', 'INFO');
      }
    };

    const markStreamSeen = (streamKey: 'A1' | 'D4' | 'E1' | 'C1') => {
      const stream = streamHealthRef.current[streamKey];
      const previousState = stream.state;

      stream.lastSeen = Date.now();
      stream.state = 'live';

      if (previousState === 'stale') {
        generateLog(`[STREAM] ${streamKey} recovered.`, 'INFO');
      } else if (previousState === 'never') {
        generateLog(`[STREAM] ${streamKey} active.`, 'INFO');
      }
    };

    if (sourceMode !== 'force-mock') {
      streamMonitorInterval = setInterval(() => {
        const now = Date.now();
        (Object.keys(streamHealthRef.current) as Array<'A1' | 'D4' | 'E1' | 'C1'>).forEach(
          (streamKey) => {
            const stream = streamHealthRef.current[streamKey];
            const staleAfter = streamStaleTimeoutMs[streamKey];
            if (stream.state !== 'live' || stream.lastSeen === null) return;
            if (now - stream.lastSeen <= staleAfter) return;

            stream.state = 'stale';
            generateLog(
              `[STREAM] ${streamKey} stale: no packets for ${(now - stream.lastSeen)} ms.`,
              'WARN'
            );
          }
        );
      }, 1000);
    }

    const connectWebSocket = () => {
      // Dacă forțăm Mock, ignorăm WebSocket-ul
      if (sourceMode === 'force-mock') return;
      
      setDataSource('detecting');
      generateLog(`Connecting to local ESP32 at ${WS_URL}...`, 'INFO');
      
      try {
        ws = new WebSocket(WS_URL);
        ws.binaryType = "arraybuffer";

        ws.onopen = () => {
          setDataSource('esp');
          generateLog('Connected to local ESP32 WebSocket!', 'INFO');
        };

        ws.onmessage = (event) => {
          try {
            if (!(event.data instanceof ArrayBuffer)) return;

            const buffer = event.data;
            const view = new DataView(buffer);
            const header = view.getUint8(0);

            if (header === 0xC1) {
              markStreamSeen('C1');
              if (buffer.byteLength < 17) {
                generateThrottledLog(
                  'c1-length',
                  `[PARSER] Invalid C1 packet length: ${buffer.byteLength} bytes.`,
                  'WARN',
                  5000
                );
                return;
              }

              markDataRestoredIfNeeded();

              const baseState = telemetryStateRef.current;
              const gpio4 = view.getUint8(1);
              const gpio3 = view.getUint8(2);
              const gpio2 = view.getUint8(3);
              const gpio1 = view.getUint8(4);
              const gpio21 = view.getUint8(5);
              const gpio20 = view.getUint8(6);
              const gpio10 = view.getUint8(7);
              const gpio9 = view.getUint8(8);
              const gpio8 = view.getUint8(9);
              const gpio7 = view.getUint8(10);
              const gpio6 = view.getUint8(11);
              const gpio5 = view.getUint8(12);

              const thermistorConnected = view.getUint8(13) !== 0;
              const rtcConnected = view.getUint8(14) !== 0;
              const gyroscopeConnected = view.getUint8(15) !== 0;
              const ina219Connected = view.getUint8(16) !== 0;

              const nextPins: TelemetryData['pins'] = {
                ...baseState.pins,
                gpio4: binaryToPinStatus(gpio4),
                gpio3: binaryToPinStatus(gpio3),
                gpio2: binaryToPinStatus(gpio2),
                gpio1: binaryToPinStatus(gpio1),
                gpio21: binaryToPinStatus(gpio21),
                gpio20: binaryToPinStatus(gpio20),
                gpio10: binaryToPinStatus(gpio10),
                gpio9: binaryToPinStatus(gpio9),
                gpio8: binaryToPinStatus(gpio8),
                gpio7: binaryToPinStatus(gpio7),
                gpio6: binaryToPinStatus(gpio6),
                gpio5: binaryToPinStatus(gpio5),
              };

              const nextModuleHealth: TelemetryData['moduleHealth'] = {
                hasPinsReport: true,
                thermistorConnected,
                rtcConnected,
                gyroscopeConnected,
                ina219Connected,
              };

              if (!baseState.moduleHealth.hasPinsReport) {
                generateLog('[C1] Initial pins and module health snapshot received.', 'INFO');
              } else {
                (Object.keys(nextPins) as Array<keyof TelemetryData['pins']>).forEach((pinKey) => {
                  if (baseState.pins[pinKey] === nextPins[pinKey]) return;
                  generateLog(
                    `[PIN] ${pinKey.toUpperCase()} changed ${baseState.pins[pinKey]} -> ${nextPins[pinKey]}.`,
                    nextPins[pinKey] === 'ok' ? 'INFO' : 'WARN'
                  );
                });

                const moduleFieldLabels: Record<
                  Exclude<keyof TelemetryData['moduleHealth'], 'hasPinsReport'>,
                  string
                > = {
                  thermistorConnected: 'NTC',
                  rtcConnected: 'RTC',
                  gyroscopeConnected: 'BMI160',
                  ina219Connected: 'INA219',
                };

                (
                  ['thermistorConnected', 'rtcConnected', 'gyroscopeConnected', 'ina219Connected'] as const
                ).forEach((moduleKey) => {
                  if (baseState.moduleHealth[moduleKey] === nextModuleHealth[moduleKey]) return;
                  const nextStatus = nextModuleHealth[moduleKey] ? 'connected' : 'disconnected';
                  generateLog(
                    `[MODULE] ${moduleFieldLabels[moduleKey]} ${nextStatus}.`,
                    nextModuleHealth[moduleKey] ? 'INFO' : 'WARN'
                  );
                });
              }

              commitTelemetryState({
                ...baseState,
                connected: true,
                timestamp: Date.now(),
                pins: nextPins,
                moduleHealth: nextModuleHealth,
              });

              resetWatchdog();
              return;
            }

            if (header === 0xD4) {
              markStreamSeen('D4');
              if (buffer.byteLength < 22) {
                generateThrottledLog(
                  'd4-length',
                  `[PARSER] Invalid D4 packet length: ${buffer.byteLength} bytes.`,
                  'WARN',
                  5000
                );
                return;
              }

              markDataRestoredIfNeeded();

              // Procesare Telemetrie (1Hz) - Binar
              // Suporta layoutul vechi D4 (22 bytes) si layoutul extins cu totalMah (26 bytes).
              const temp = view.getFloat32(5, true);
              const volt = view.getFloat32(9, true);
              const curr = view.getFloat32(13, true);

              let bat = view.getUint8(17);
              let cpu = view.getUint8(18);
              let totalMah = curr;

              // Detectam layoutul extins in care totalMah este inserat dupa curr.
              if (buffer.byteLength >= 26) {
                const candidateTotalMah = view.getFloat32(17, true);
                const candidateBat = view.getUint8(21);
                const candidateCpu = view.getUint8(22);
                const candidateRtcMin = view.getUint8(24);
                const candidateRtcSec = view.getUint8(25);

                const looksLikeShiftedLayout =
                  candidateBat <= 100 &&
                  candidateCpu <= 100 &&
                  candidateRtcMin <= 59 &&
                  candidateRtcSec <= 59;

                if (looksLikeShiftedLayout) {
                  totalMah = candidateTotalMah;
                  bat = candidateBat;
                  cpu = candidateCpu;
                } else {
                  // Fallback: unele versiuni pot adauga totalMah la final, pastrand offseturile vechi.
                  totalMah = view.getFloat32(22, true);
                }
              }

              // Firmware-ul nu trimite inca battery_life in D4,
              // asa ca il calculam local cu aceeasi logica de pe OLED.
              const batteryLifeStr = (() => {
                if (volt > 4.5) return 'N/A';

                if (curr <= 0 || bat <= 0) return '---';

                const hours = (3200.0 * (bat / 100.0)) / curr;
                if (!Number.isFinite(hours) || hours < 0) return '---';

                // Afisam direct TimpRamasOre calculat, fara plafonare la >99h.
                return `${hours.toFixed(2)}h`;
              })();

              pushPayloadToState(
                {
                  temperature: temp,
                  io_log: `Telemetry Info: CPU ${cpu}% Temp ${temp.toFixed(1)}C`,
                  timestamp: new Date().toISOString(),
                  gpio_pin: 'ALL_PINS',
                  rssi: -50, // Default sau parsat din alt pachet dacă e adăugat
                  network_name: 'HS_IMU_STREAM',
                  cpu_load: cpu,
                  voltage: volt,
                  battery_percentage: bat,
                  current_now: curr,
                  current_total: totalMah,
                  battery_life: batteryLifeStr,
                },
                { allowImuUpdate: false, allowPowerUpdate: true }
              );
              return;
            }

            if (header === 0xA1 || header === 0xE1) {
              markStreamSeen(header === 0xA1 ? 'A1' : 'E1');

              // Acceptam atat structuri packed (21 bytes), cat si structuri aliniate pe 4 bytes (24 bytes).
              const isPackedStruct = buffer.byteLength === 21;
              const isAlignedStruct = buffer.byteLength === 24;
              if (!isPackedStruct && !isAlignedStruct) {
                generateThrottledLog(
                  `imu-length-${header}`,
                  `[PARSER] Invalid ${header === 0xA1 ? 'A1' : 'E1'} packet length: ${buffer.byteLength} bytes.`,
                  'WARN',
                  5000
                );
                return;
              }

              markDataRestoredIfNeeded();

              // In varianta aliniata, compiler-ul poate adauga 3 bytes padding dupa header.
              const baseOffset = isPackedStruct ? 1 : 4;
              const sequence = view.getUint32(baseOffset, true);
              const sampleMicros = view.getUint32(baseOffset + 4, true);
              const gx = view.getInt16(baseOffset + 8, true);
              const gy = view.getInt16(baseOffset + 10, true);
              const gz = view.getInt16(baseOffset + 12, true);
              const ax = view.getInt16(baseOffset + 14, true);
              const ay = view.getInt16(baseOffset + 16, true);
              const az = view.getInt16(baseOffset + 18, true);

              // Pachet status: pastram ultima orientare valida, dar marcam conexiunea activa.
              if (header === 0xE1) {
                lastSampleMicrosRef.current = sampleMicros;
                const baseState = telemetryStateRef.current;
                commitTelemetryState({
                  ...baseState,
                  connected: true,
                  timestamp: Date.now(),
                });
                resetWatchdog();
                return;
              }

              const previousSequence = lastImuSequenceRef.current;
              if (previousSequence !== null) {
                if (sequence <= previousSequence) {
                  generateThrottledLog(
                    'imu-sequence-rollback',
                    `[IMU] Sequence rollback detected: ${previousSequence} -> ${sequence}.`,
                    'WARN',
                    2000
                  );
                } else if (sequence > previousSequence + 1) {
                  generateLog(`[IMU] Sequence gap detected: expected ${previousSequence + 1}, got ${sequence}.`, 'WARN');
                }
              }
              lastImuSequenceRef.current = sequence;

              const orientationDeg = orientationDegRef.current;
              const previousSampleMicros = lastSampleMicrosRef.current;

              if (previousSampleMicros !== null) {
                if (sampleMicros <= previousSampleMicros) {
                  generateThrottledLog(
                    'imu-samplemicros-rollback',
                    `[IMU] sampleMicros rollback detected: ${previousSampleMicros} -> ${sampleMicros}.`,
                    'WARN',
                    2000
                  );
                } else if (sampleMicros - previousSampleMicros > 100000) {
                  generateThrottledLog(
                    'imu-samplemicros-gap',
                    `[IMU] sampleMicros jump detected: +${sampleMicros - previousSampleMicros} us.`,
                    'WARN',
                    5000
                  );
                }
              }

              // Exact ca in app-ul mobil: integrare simpla gyro pe fiecare axa.
              orientationDeg.x = normalizeAngleDeg(orientationDeg.x + gx / 500);
              orientationDeg.y = normalizeAngleDeg(orientationDeg.y + gy / 500);
              orientationDeg.z = normalizeAngleDeg(orientationDeg.z + gz / 500);

              const accelMagnitude = Math.sqrt(ax * ax + ay * ay + az * az);
              if (accelMagnitude > 1e-3) {
                const accelAngleX = (Math.atan2(ay, az) * 180) / Math.PI;
                const accelAngleY = (Math.atan2(-ax, Math.sqrt(ay * ay + az * az)) * 180) / Math.PI;
                const hasSampleDelta = previousSampleMicros !== null && sampleMicros > previousSampleMicros;

                if (hasSampleDelta) {
                  const alpha = 0.96;
                  orientationDeg.x = normalizeAngleDeg(
                    alpha * orientationDeg.x + (1 - alpha) * accelAngleX
                  );
                  orientationDeg.y = normalizeAngleDeg(
                    alpha * orientationDeg.y + (1 - alpha) * accelAngleY
                  );
                }
              }

              lastSampleMicrosRef.current = sampleMicros;

              const baseState = telemetryStateRef.current;
              commitTelemetryState({
                ...baseState,
                connected: true,
                timestamp: Date.now(),
                imu: {
                  ...baseState.imu,
                  roll: degToRad(orientationDeg.x),
                  pitch: degToRad(orientationDeg.y),
                  yaw: degToRad(orientationDeg.z),
                  accel: accelMagnitude,
                  gyroX: gx,
                  gyroY: gy,
                  gyroZ: gz,
                  accelX: ax,
                  accelY: ay,
                  accelZ: az,
                  sequence,
                  sampleMicros,
                },
              });

              resetWatchdog();
              return;
            }

            generateThrottledLog(
              `unknown-header-${header}`,
              `[PARSER] Unknown header received: 0x${header.toString(16).toUpperCase()}.`,
              'WARN',
              5000
            );
          } catch (err) {
            generateThrottledLog(
              'parser-exception',
              '[PARSER] Error while decoding packet payload.',
              'ERROR',
              3000
            );
            console.error('Error processing websocket message', err);
          }
        };

        ws.onclose = () => {
          generateLog('WebSocket disconnected. Reconnecting in 5s...', 'WARN');
          reconnectTimeout = setTimeout(connectWebSocket, 5000);
        };

        ws.onerror = () => {
          // Silent pe err ptr ca triggereaza anyway onclose
          ws?.close(); 
        };
      } catch (err) {
        generateLog('WebSocket Connect Error', 'ERROR');
      }
    };

    if (sourceMode === 'auto' || sourceMode === 'force-esp') {
      connectWebSocket();
    }

    // Verificare conexiune la fiecare 10 secunde, daca nu primim date, consideram ca e offline
    const resetWatchdog = () => {
      if (watchdogRef.current) clearTimeout(watchdogRef.current);
      watchdogRef.current = setTimeout(() => {
        const baseState = telemetryStateRef.current;
        commitTelemetryState({
          ...baseState,
          connected: false,
          timestamp: Date.now(),
        });
        if (sourceMode !== 'force-mock') {
            generateLog('Connection lost: No data received for 10 seconds.', 'WARN');
        }
      }, 10000);
    };

    // Date prelucrate din BINARY sau MOCK pentru enviroment, power si system
    const pushPayloadToState = (
      esp32Payload: Esp32Payload,
      options?: { allowImuUpdate?: boolean; allowPowerUpdate?: boolean }
    ) => {
      const { allowImuUpdate = true, allowPowerUpdate = true } = options ?? {};
      const smoothedTemp = tempSmoother.current.addValue(esp32Payload.temperature);

      const baseState = telemetryStateRef.current;
      const parsedTimestamp = new Date(esp32Payload.timestamp).getTime();
      const safeTimestamp = Number.isFinite(parsedTimestamp) ? parsedTimestamp : Date.now();

      const newData: TelemetryData = {
        ...baseState,
        timestamp: safeTimestamp,
        connected: true,
        environment: {
          ...baseState.environment,
          temperature: Number(smoothedTemp.toFixed(1)),
        },
        imu:
          allowImuUpdate && esp32Payload.imu
            ? {
                ...baseState.imu,
                ...esp32Payload.imu,
              }
            : baseState.imu,
        power: allowPowerUpdate
          ? {
              ...baseState.power,
              voltage: esp32Payload.voltage ?? baseState.power.voltage,
              current: esp32Payload.current_now ?? baseState.power.current,
              batteryPercentage:
                esp32Payload.battery_percentage ?? baseState.power.batteryPercentage,
              totalEnergy: esp32Payload.current_total ?? baseState.power.totalEnergy,
              batteryLifeStr: esp32Payload.battery_life ?? baseState.power.batteryLifeStr,
            }
          : baseState.power,
        system: {
          ...baseState.system,
          cpuLoad: esp32Payload.cpu_load ?? baseState.system.cpuLoad,
          network: {
            ...baseState.system.network,
            ssid: esp32Payload.network_name ?? baseState.system.network.ssid,
            rssi: esp32Payload.rssi ?? baseState.system.network.rssi,
          }
        }
      };

      commitTelemetryState(newData, true);

      const now = Date.now();
      if (now - lastLogTimeRef.current >= 1000 && esp32Payload.io_log) {
        generateLog(esp32Payload.io_log, 'DATA');
        lastLogTimeRef.current = now;
      }
      resetWatchdog();
    };

    // ----- LOGICA MOCK DATE -----
    if (sourceMode === 'force-mock') {
      setDataSource('mock');
      generateLog('Started Mock Generator', 'INFO');
      
      let mockAngle = 0;
      const mockStepMs = 5000;
      const mockStartTime = Date.now();
      const ninetyDeg = Math.PI / 2;
      const mockPoses: Array<{ roll: number; pitch: number; yaw: number }> = [
        { roll: 0, pitch: 0, yaw: 0 },
        { roll: ninetyDeg, pitch: 0, yaw: 0 },
        { roll: 0, pitch: ninetyDeg, yaw: 0 },
        { roll: 0, pitch: 0, yaw: ninetyDeg },
      ];

      mockIntervalRef.current = setInterval(() => {
        mockAngle += 0.1;
        const fakeTemp = 24.5 + Math.sin(mockAngle) * 15; // Fluctueaza de la +9 pana la +39
        const poseIndex = Math.floor((Date.now() - mockStartTime) / mockStepMs) % mockPoses.length;
        const pose = mockPoses[poseIndex];
        
        pushPayloadToState({
            temperature: fakeTemp,
            io_log: "MOCK_DATA_TICK",
            timestamp: new Date().toISOString(),
            gpio_pin: "ALL_PINS",
            rssi: -30,
            network_name: "Mock_Network",
            cpu_load: 40 + Math.random() * 20,
            voltage: 4.1,
            battery_percentage: 95,
            current_now: 120 + Math.random() * 10,
            current_total: 50,
            battery_life: `${((3200 * (95 / 100)) / (120 + Math.random() * 10)).toFixed(2)}h`,
            imu: {
              pitch: pose.pitch,
              roll: pose.roll,
              yaw: pose.yaw,
              accel: 1.0
            }
        });
      }, 1000);
    }

    // Cleanup la demontare sau cand se schimba setarile de conexiune
    return () => {
      if (watchdogRef.current) clearTimeout(watchdogRef.current);
      if (reconnectTimeout) clearTimeout(reconnectTimeout);
      if (streamMonitorInterval) clearInterval(streamMonitorInterval);
      if (mockIntervalRef.current) clearInterval(mockIntervalRef.current);
      if (ws) {
        ws.onclose = null; // Pentru a nu face trigger la reconnect
        ws.close();
      }
    };
  }, [sourceMode]);

  return { currentData, dataHistory, logs, dataSource, sourceMode, setSourceMode };
}

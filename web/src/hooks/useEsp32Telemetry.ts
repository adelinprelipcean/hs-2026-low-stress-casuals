import { useState, useEffect, useRef } from 'react';
import type { TelemetryData, IoLogEntry, Esp32Payload } from '../types/telemetry';
import { SmaSmoother } from '../lib/smoothing';

// Setam IP-ul local al ESP32-ului (In mod Access Point, default este 192.168.4.1)
// Portul 3333 este comun pentru librariile de WebSockets pe C++/ESP32
const WS_URL = "ws://192.168.4.1:3333";

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
  const lastSampleMicrosRef = useRef<number | null>(null);
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

  // websocket connection and payload handling
  useEffect(() => {
    let ws: WebSocket | null = null;
    let reconnectTimeout: ReturnType<typeof setTimeout>;

    lastSampleMicrosRef.current = null;
    orientationDegRef.current = { x: 0, y: 0, z: 0 };

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
            if (event.data instanceof ArrayBuffer) {
              const buffer = event.data;
              const view = new DataView(buffer);
              const header = view.getUint8(0);

              if (header === 0xD4 && buffer.byteLength >= 22) {
                // Procesare Telemetrie (1Hz) - Binar
                const temp = view.getFloat32(5, true);
                const volt = view.getFloat32(9, true);
                const curr = view.getFloat32(13, true);
                const bat = view.getUint8(17);
                const cpu = view.getUint8(18);
                
                pushPayloadToState({
                  temperature: temp,
                  io_log: `Telemetry Info: CPU ${cpu}% Temp ${temp.toFixed(1)}C`,
                  timestamp: new Date().toISOString(),
                  gpio_pin: "ALL_PINS",
                  rssi: -50, // Default sau parsat din alt pachet dacă e adăugat
                  network_name: "HS_IMU_STREAM",
                  cpu_load: cpu,
                  voltage: volt,
                  battery_percentage: bat,
                  current_now: curr,
                  current_total: curr, // Mock total deocamdată
                  battery_life: volt > 4.5 ? "N/A" : ">99h"
                });
              } else if (header === 0xA1 || header === 0xE1) {
                // Acceptam atat structuri packed (21 bytes), cat si structuri aliniate pe 4 bytes (24 bytes).
                const isPackedStruct = buffer.byteLength === 21;
                const isAlignedStruct = buffer.byteLength === 24;
                if (!isPackedStruct && !isAlignedStruct) return;

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
                  resetWatchdog();
                  return;
                }

                const orientationDeg = orientationDegRef.current;
                const previousSampleMicros = lastSampleMicrosRef.current;

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

                setCurrentData((prev) => {
                  const baseState = prev || {
                    timestamp: Date.now(),
                    connected: true,
                    environment: { temperature: 25.0 },
                    imu: { pitch: 0, roll: 0, yaw: 0, accel: 1.0 },
                    power: { voltage: 4.1, current: 0, batteryPercentage: 100, totalEnergy: 0, batteryLifeStr: "99h" },
                    system: { cpuLoad: 15, network: { ssid: "HS_IMU_STREAM", rssi: -50 } }
                  };

                  return {
                    ...baseState,
                    connected: true,
                    timestamp: Date.now(),
                    imu: {
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
                  };
                });

                resetWatchdog();
              }
            }
          } catch (err) {
            console.error("Error processing websocket message", err);
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
        setCurrentData(prev => prev ? { ...prev, connected: false } : null);
        if (sourceMode !== 'force-mock') {
            generateLog('Connection lost: No data received for 10 seconds.', 'WARN');
        }
      }, 10000);
    };

    // Date prelucrate din BINARY sau MOCK pentru enviroment, power si system
    const pushPayloadToState = (esp32Payload: Esp32Payload) => {
      const smoothedTemp = tempSmoother.current.addValue(esp32Payload.temperature);

      const newData: TelemetryData = {
        timestamp: new Date(esp32Payload.timestamp).getTime() || Date.now(),
        connected: true,
        environment: {
          temperature: Number(smoothedTemp.toFixed(1)),
        },
        imu: esp32Payload.imu
          ? {
              ...esp32Payload.imu,
            }
          : {
              pitch: 0,
              roll: 0,
              yaw: 0,
              accel: 1.0,
            },
        power: {
          voltage: esp32Payload.voltage,
          current: esp32Payload.current_now,
          batteryPercentage: esp32Payload.battery_percentage,
          totalEnergy: esp32Payload.current_total,
          batteryLifeStr: esp32Payload.battery_life,
        },
        system: {
          cpuLoad: esp32Payload.cpu_load,
          network: {
            ssid: esp32Payload.network_name,
            rssi: esp32Payload.rssi,
          }
        }
      };

      setCurrentData(newData);
      setDataHistory((prev) => [...prev, newData].slice(-60));

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
            battery_life: "99h",
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
      if (mockIntervalRef.current) clearInterval(mockIntervalRef.current);
      if (ws) {
        ws.onclose = null; // Pentru a nu face trigger la reconnect
        ws.close();
      }
    };
  }, [sourceMode]);

  return { currentData, dataHistory, logs, dataSource, sourceMode, setSourceMode };
}

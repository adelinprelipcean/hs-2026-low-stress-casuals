import { useState, useEffect, useRef } from 'react';
import type { TelemetryData, IoLogEntry, Esp32Payload } from '../types/telemetry';
import { SmaSmoother } from '../lib/smoothing';
import mqtt from 'mqtt';

const MQTT_SERVER = "wss://13c35f0a5bde4564be3ea561c26c7c3b.s1.eu.hivemq.cloud:8884/mqtt";
const MQTT_USER = "esp32mqtt";
const MQTT_PASS = "L7sqBG*9+w2m";
const MQTT_TOPIC = "#"; // Using '#' to listen to all topics, since we don't know the exact one

export function useEsp32Telemetry() {
  const [dataHistory, setDataHistory] = useState<TelemetryData[]>([]);
  const [currentData, setCurrentData] = useState<TelemetryData | null>(null);
  const [logs, setLogs] = useState<IoLogEntry[]>([]);
  const [dataSource] = useState<'detecting' | 'esp' | 'mock'>('esp');

  const tempSmoother = useRef(new SmaSmoother(5));
  const lightSmoother = useRef(new SmaSmoother(5));

  useEffect(() => {
    const generateLog = (msg: string, type: IoLogEntry['type'] = 'INFO') => {
      const entry: IoLogEntry = {
        id: Math.random().toString(36).substring(7),
        timestamp: Date.now(),
        type,
        message: msg
      };
      setLogs((prev) => [entry, ...prev].slice(0, 100)); // Keep last 100
    };

    generateLog('Connecting to HiveMQ Cloud via WebSockets...', 'INFO');

    const client = mqtt.connect(MQTT_SERVER, {
      username: MQTT_USER,
      password: MQTT_PASS,
      clientId: `web-client-${Math.random().toString(16).substring(2, 8)}`,
      reconnectPeriod: 5000,
    });

    client.on('connect', () => {
      generateLog('Connected to HiveMQ Cloud!', 'INFO');
      client.subscribe(MQTT_TOPIC, (err) => {
        if (!err) {
          generateLog(`Subscribed to topic: ${MQTT_TOPIC}`, 'INFO');
        } else {
          generateLog(`Failed to subscribe: ${err.message}`, 'ERROR');
        }
      });
    });

    client.on('message', (_topic, message) => {
      try {
        const payloadStr = message.toString();
        const epsPayload = JSON.parse(payloadStr) as Esp32Payload;
        
        // Ensure this looks like our payload by checking a key (time or cpu_load_pct)
        if (epsPayload.cpu_load !== undefined) {
          pushPayloadToState(epsPayload);
        }
      } catch (err) {
        // Ignored, might be random message
      }
    });

    client.on('error', (err) => {
      generateLog(`MQTT Error: ${err.message}`, 'ERROR');
    });

    const pushPayloadToState = (esp32Payload: Esp32Payload) => {
      const smoothedTemp = tempSmoother.current.addValue(esp32Payload.temperature);
      const smoothedLight = lightSmoother.current.addValue(esp32Payload.light_intensity);

      const newData: TelemetryData = {
        timestamp: new Date(esp32Payload.timestamp).getTime() || Date.now(),
        connected: true,
        environment: {
          temperature: Number(smoothedTemp.toFixed(1)),
          lightIntensity: Number(smoothedLight.toFixed(0)),
        },
        power: {
          voltage: esp32Payload.voltage,
          current: esp32Payload.current_now,
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

      generateLog(`[${esp32Payload.gpio_pin}] ${esp32Payload.io_log}`, 'DATA');
    };

    return () => {
      client.end();
    };
  }, []);

  return { currentData, dataHistory, logs, dataSource, sourceMode: 'auto', setSourceMode: (_mode: 'auto' | 'force-esp' | 'force-mock') => {} };
}

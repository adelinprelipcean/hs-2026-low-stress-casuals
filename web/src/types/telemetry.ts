/**
 * The exact raw JSON payload expected from the ESP32-C3
 */
export interface Esp32Payload {
  temperature: number;
  light_intensity: number;
  io_log: string;
  timestamp: string; // ISO 8601 datetime
  gpio_pin: string;
  rssi: number;
  network_name: string;
  cpu_load: number;
  voltage: number;
  battery_percentage: number;
  current_now: number;
  current_total: number;
  battery_life: string;
}

/**
 * The transformed internal state used by the UI components
 */
export interface TelemetryData {
  timestamp: number;
  environment: {
    temperature: number;
    lightIntensity: number;
  };
  power: {
    voltage: number;
    current: number;
    batteryPercentage: number;
    totalEnergy: number; // accumulated mAh or Joules
    batteryLifeStr: string; // directly mapping the ESP32 string
  };
  system: {
    cpuLoad: number;
    network: {
      ssid: string;
      rssi: number;
    };
  };
  connected: boolean;
}

export interface IoLogEntry {
  id: string;
  timestamp: number;
  type: 'INFO' | 'WARN' | 'ERROR' | 'DATA';
  message: string;
}

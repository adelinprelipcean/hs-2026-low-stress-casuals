/**
 * The exact raw JSON payload expected from the ESP32-C3
 */
export interface Esp32Payload {
  temperature: number;
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
  imu?: {
    pitch: number;
    roll: number;
    yaw: number;
    accel: number;
  };
}

/**
 * The transformed internal state used by the UI components
 */
export interface TelemetryData {
  timestamp: number;
  environment: {
    temperature: number;
  };
  pins: {
    gpio5: 'ok' | 'error' | 'unknown';
    gpio6: 'ok' | 'error' | 'unknown';
    gpio7: 'ok' | 'error' | 'unknown';
    gpio8: 'ok' | 'error' | 'unknown';
    gpio9: 'ok' | 'error' | 'unknown';
    gpio10: 'ok' | 'error' | 'unknown';
    gpio20: 'ok' | 'error' | 'unknown';
    gpio21: 'ok' | 'error' | 'unknown';
    gpio4: 'ok' | 'error' | 'unknown';
    gpio3: 'ok' | 'error' | 'unknown';
    gpio2: 'ok' | 'error' | 'unknown';
    gpio1: 'ok' | 'error' | 'unknown';
    gpio0: 'ok' | 'error' | 'unknown';
    p5v: 'ok' | 'error' | 'unknown';
    gnd: 'ok' | 'error' | 'unknown';
    p3v3: 'ok' | 'error' | 'unknown';
  };
  moduleHealth: {
    hasPinsReport: boolean;
    thermistorConnected: boolean;
    rtcConnected: boolean;
    gyroscopeConnected: boolean;
    ina219Connected: boolean;
  };
  imu: {
    pitch: number;
    roll: number;
    yaw: number;
    accel: number;
    gyroX?: number;
    gyroY?: number;
    gyroZ?: number;
    accelX?: number;
    accelY?: number;
    accelZ?: number;
    sequence?: number;
    sampleMicros?: number;
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

// Log entry structure for UI display
export interface IoLogEntry {
  id: string;
  timestamp: number;
  type: 'INFO' | 'WARN' | 'ERROR' | 'DATA';
  message: string;
}

/**
 * Simple Moving Average (SMA) smoothing implementation.
 * 
 * Logic & Calculations:
 * Smoothing is used to prevent "jitter" in graph displays. Raw sensor data (e.g., ADCs on ESP32)
 * often fluctuates rapidly due to electrical noise or unstable readings. By averaging
 * the most recent N samples, we "smooth out" the curve, creating a more legible UI.
 */
export class SmaSmoother {
  private windowSize: number;
  private values: number[] = [];

  constructor(windowSize: number = 5) {
    this.windowSize = windowSize;
  }

  addValue(value: number): number {
    this.values.push(value);
    if (this.values.length > this.windowSize) {
      this.values.shift();
    }
    const sum = this.values.reduce((a, b) => a + b, 0);
    return sum / this.values.length;
  }
}

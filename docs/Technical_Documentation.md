# Technical Documentation: ESP32-C3 Firmware

This document describes the mathematical models and logic implemented in the ESP32-C3 firmware for the Hard&Soft 2026 Task 1.

## 1. CPU Load Calculation (FreeRTOS Idle Task Hook)
The CPU load is determined by measuring the amount of time the processor spends in the **Idle Task** compared to a theoretical maximum.
A FreeRTOS idle hook function `esp_register_freertos_idle_hook(idle_task_hook)` increments a volatile counter.

**Methodology:**
1. During system boot (prior to other FreeRTOS tasks being spawned), the system waits for EXACTLY `1000 ms`, counting maximum possible idle executions $\rightarrow N_{max}$.
2. During runtime, a 1Hz task reads and resets the counter $\rightarrow N_{idle}$.
3. Load Formula:
   $$ \text{Load \%} = 100 \times \left(1 - \frac{N_{idle}}{N_{max}}\right) $$

## 2. Total Consumption Integration (mAh)
To accurately display the energy drawn from the 18650 battery over time, the system uses a discrete Riemann sum integration of the instantaneous current (mA) measured by the INA219 sensor.

**Methodology:**
Given that the sensor task `vTaskSensors` runs strictly at $1\text{Hz}$ using `vTaskDelayUntil()`, the time delta $\Delta t$ is exactly 1 second. 
Since $mAh$ requires hours, $\Delta t$ in hours is $\frac{1}{3600}$.
$$ Q_{total} = Q_{old} + \sum_{n} \left( I_{inst} \times \frac{1}{3600} \right) $$
Where $I_{inst}$ is the instantaneous current in milliamperes (mA).

## 3. Battery Life Estimation
For the connected mobile/web apps to display expected battery life, the application mathematically estimates time remaining based on the battery's nominal capacity and the average continuous current draw.

**Methodology:**
Assuming a typical 18650 Lithium-Ion Battery (e.g., $2600\text{ mAh}$ nominal capacity):
$$ C_{rem} = 2600 - Q_{total} $$
$$ T_{life} \text{ (hours)} = \frac{C_{rem}}{I_{avg}} $$
Where $I_{avg}$ is a moving average of the current or simply the current $I_{inst}$ if the load is steady state.

## 4. Hardware AD/DA Conversion (PCF8591)
The **HW-011 (PCF8591)** features an 8-bit Analog-to-Digital Converter mapping an analog voltage from $0\text{V} \rightarrow V_{CC}$ (which is 3.3V on this design) to a numeric range of $0 \rightarrow 255$.

**Methodology:**
The conversion formula to extract the real-world Voltage from the raw 8-bit integer is:
$$ V_{analog} = \left( \frac{\text{ADC}_{value}}{255} \right) \times V_{ref} $$
Because the module includes onboard sensors (e.g., a photoresistor on AIN0 and a thermistor on AIN1), these raw values are transformed linearly into percentage or passed through the Steinhart-Hart equation based on the NTC characteristics.

---
_Generated for Hard&Soft 2026 Pre-selection Task._

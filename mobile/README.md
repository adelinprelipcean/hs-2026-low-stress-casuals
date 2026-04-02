# ESP32 Diagnostic Tool (Android)

This repository contains an Android app that connects to an ESP32-C3 device over local Wi-Fi, ingests a mixed stream of binary telemetry packets, and renders live diagnostics across three UI surfaces:

1. 3D orientation view (IMU-driven model rotation)
2. dashboard view (temperature, voltage, current, battery, CPU)
3. digital I/O view (GPIO states and short rolling history)

The app also includes a synthetic data mode for offline demo/testing, and it persists user display preferences for temperature units.

## 1) What this project is doing, at a high level

At runtime, the app does the following loop:

1. Starts `MainActivity`.
2. Creates `MainViewModel` (single shared state holder for all fragments).
3. Opens a WebSocket to `ws://192.168.4.1:3333/` via `SocketManager`.
4. Reads incoming frame bytes and parses three packet types:
   - IMU packet (`0xA1`)
   - Telemetry packet (`0xD4`)
   - GPIO packet (`0xC1`)
5. Pushes parsed packets into LiveData in `MainViewModel`.
6. Three tabs observe shared LiveData and update independently:
   - `ThreeDFragment`: IMU fusion + 3D model rotation in WebView
   - `MainDashboardFragment`: charting + metric cards
   - `IoFragment`: pin indicator LEDs + stepped history plots
7. If WebSocket is unavailable or user toggles demo mode, a mock generator emits packets every 250 ms and drives the same UI path.

Everything in the UI is therefore fed by one of two interchangeable sources:

- real packet stream from ESP32 over WebSocket
- synthetic packet stream from in-app generator

## 2) Build system and platform setup

### Root Gradle

- Build scripts use Kotlin DSL (`*.gradle.kts`).
- Android Gradle Plugin: `8.7.3`
- Kotlin: `2.0.21`
- Root includes a single module: `:app`

### Android target config (`app/build.gradle.kts`)

- `compileSdk = 35`
- `targetSdk = 35`
- `minSdk = 26`
- Java/Kotlin bytecode target: `11`
- ViewBinding: enabled
- Release minification: disabled (`isMinifyEnabled = false`)

### Repository sources

The project resolves dependencies from:

- `google()`
- `mavenCentral()`
- JitPack (for MPAndroidChart)
- Eclipse Paho releases repo (legacy MQTT artifact source)

### Main runtime libraries

- AndroidX AppCompat, Activity, Fragment, ConstraintLayout
- Material Components / Material3
- Lifecycle ViewModel + LiveData
- ViewPager2 + TabLayoutMediator
- OkHttp (WebSocket transport)
- MPAndroidChart (line and stepped graphs)
- Gson (model serialization annotations)
- AndroidX WebKit + WebViewAssetLoader
- Filament libraries are present in dependencies but currently not used by active rendering path

## 3) Manifest-level behavior and network assumptions

`AndroidManifest.xml` enables:

- `android.permission.INTERNET`
- `android:usesCleartextTraffic="true"`

Cleartext is required because the app connects to non-TLS `ws://` endpoint on local network.

Main launcher activity:

- `MainActivity` (exported true, LAUNCHER)

Secondary internal activity:

- `SettingsActivity` (exported false)

## 4) Source layout and responsibilities

Top package: `com.example.esp32_diagtool`

### Core orchestration

- `MainActivity.kt`
  - owns tabbed navigation
  - starts/stops socket connection
  - toggles mock mode
  - adapts telemetry packet to display model (`EspData`)
  - monitors sensor connected/disconnected edge changes and emits toast notifications

- `MainViewModel.kt`
  - LiveData publisher for:
    - latest ESP telemetry (`espData`)
    - latest IMU packet (`imuData`)
    - latest GPIO map (`gpioStates`)
    - append-only telemetry history (`logHistory`)
  - holds session `startTime` for chart x-axis timing

### Networking

- `network/SocketManager.kt`
  - WebSocket client (OkHttp)
  - reconnect loop in dedicated thread
  - frame-to-packet parser with partial-frame carryover buffer (`pendingBytes`)

### UI fragments

- `fragments/ThreeDFragment.kt`
  - WebView + Three.js asset host
  - IMU complementary filtering and bias correction
  - euler rotation bridge to JavaScript
  - axis gizmo overlay and recenter logic

- `fragments/MainDashboardFragment.kt`
  - battery, CPU, temp/voltage/current card UI
  - three line charts with moving 30-second viewport
  - temperature unit conversion using persisted preference

- `fragments/IoFragment.kt`
  - ESP32-C3 board image overlay with pin indicators
  - pin histories with stepped charts
  - fixed HIGH/LOW rails for 5V/GND/3.3V

### Extra/legacy UI pieces

- `fragments/LogFragment.kt` + `fragments/LogAdapter.kt`
  - a searchable log-history RecyclerView implementation exists
  - currently not wired into `MainActivity` tab pager

- `views/ImuVisualizerView.kt`
  - OpenGL ES cube renderer exists as alternative IMU visualization
  - currently not used by active fragment layout (WebView path is used)

- `mqtt/MqttManager.kt`
  - intentionally empty stub; MQTT path deprecated in favor of socket manager

## 5) Runtime execution flow in detail

### App start

On `MainActivity.onCreate`:

1. Inflate `ActivityMainBinding`.
2. Attach `MaterialToolbar` as support action bar.
3. Build ViewPager adapter with fixed 3-fragment order:
   - index 0: `ThreeDFragment`
   - index 1: `MainDashboardFragment`
   - index 2: `IoFragment`
4. Bind tab text via `TabLayoutMediator`.
5. Initialize and connect `SocketManager`.
6. Restore `use_mock_data` flag from `app_debug_prefs`.
7. Apply either socket mode or mock mode.

### Data source switching

Menu item: `Use Mock Data` (checkable, runtime toggle)

When turning mock ON:

- disconnects socket manager
- starts periodic mock runnable on main thread handler
- posts synthetic IMU, telemetry, and GPIO packets every 250 ms

When turning mock OFF:

- removes pending runnable callbacks
- reconnects socket manager

Preference key:

- file: `app_debug_prefs`
- key: `use_mock_data`

### Socket thread lifecycle (`SocketManager`)

`connect()` spawns thread `SocketManager-Reader` which loops while `isRunning`:

1. Build WebSocket request.
2. Attach listener callbacks (`onOpen`, `onMessage`, `onFailure`, etc.).
3. Wait until connected or closed.
4. On close/failure, clear state and sleep 1500 ms before reconnect attempt.

Important operational constants:

- host: `192.168.4.1`
- port: `3333`
- path: `/`
- connect timeout: `5000 ms`
- reconnect delay: `1500 ms`
- read timeout: `0` (infinite) for continuous stream

### Frame parsing and packet extraction

Incoming WebSocket frames may be:

- binary (`ByteString`) or
- text (`String`)

Text frame conversion path:

- trims input
- if looks like even-length hex, decodes hex to bytes
- else interprets string as ISO-8859-1 raw bytes

The parser keeps a `pendingBytes` tail for incomplete packets. Each parse pass:

1. Concatenates `pendingBytes + newData`.
2. Reads leading header byte.
3. Dispatches by known header and expected packet length.
4. If insufficient bytes for full packet, stops and preserves tail.
5. Unknown headers are skipped byte-by-byte with warning counts.

On parse exceptions, pending buffer is cleared to recover quickly.

## 6) Binary packet protocol (exact schema)

All packets are little-endian payloads with 1-byte type header.

### A) IMU packet (`0xA1`)

Total size: `21 bytes` = `1 header + 20 payload`

Payload fields:

- `uint32 sample sequence`
- `uint32 sampleMicros`
- `int16 gyroX`
- `int16 gyroY`
- `int16 gyroZ`
- `int16 accelX`
- `int16 accelY`
- `int16 accelZ`

Mapped to `ImuStreamPacket`.

### B) Telemetry packet (`0xD4`)

Total size: `22 bytes` = `1 header + 21 payload`

Payload fields:

- `uint32 sampleMs`
- `float32 temp`
- `float32 volt`
- `float32 curr`
- `uint8 bat` (battery percentage)
- `uint8 cpu` (cpu load)
- `uint8 rtcHour`
- `uint8 rtcMin`
- `uint8 rtcSec`

Mapped to `TelemetryPacket`, then converted to display model `EspData`.

### C) GPIO packet (`0xC1`)

Total size: `18 bytes` = `1 header + 17 payload`

Payload fields (all `uint8`):

- digital pins:
  - `gpio4, gpio3, gpio2, gpio1, gpio0, gpio21, gpio20, gpio10, gpio9, gpio8, gpio7, gpio6, gpio5`
- sensor presence flags:
  - `thermistorIsConnected`
  - `i2cInaIsConnected`
  - `i2cRtcIsConnected`
  - `i2cGyroIsConnected`

Mapped to `GpioPacket` and then to `Map<String, Boolean>` in ViewModel.

## 7) Shared state model and LiveData fan-out

### `MainViewModel` channels

- `espData`: most recent normalized telemetry snapshot
- `imuData`: most recent raw IMU sample packet
- `gpioStates`: current binary pin state map
- `logHistory`: growing list of `EspDataPoint(data, receivedAt)`

All fragment rendering is observer-driven; fragments do not pull from socket directly.

### History behavior

- History is append-only in memory.
- No pruning policy currently exists.
- Long sessions can grow memory usage due to unlimited `historyList`.

## 8) 3D IMU rendering path (Android -> WebView -> Three.js)

### Android side (`ThreeDFragment`)

`ThreeDFragment` does sensor fusion from raw packet fields:

1. Computes dynamic sample delta from `sampleMicros` with wraparound handling.
2. Converts gyro raw to dps using `gyroLsbPerDps = 131`.
3. Learns gyro bias only under quasi-stationary conditions.
4. Integrates bias-corrected gyro rates into euler angles.
5. Uses accelerometer tilt for long-term correction (complementary filter).
6. Applies recenter offsets.
7. Pushes final euler angles to WebView JavaScript bridge.

Key filter constants:

- `accelCorrectionTimeConstantSec = 0.55`
- `defaultSampleDeltaSec = 0.01`
- `gyroBiasLearnRate = 0.02`
- `stationaryGyroThresholdDps = 1.5`
- `accelReliabilityTolerance = 0.12`

Web bridge call format:

- `window.updateImuRotation(xDeg, yDeg, zDeg)`

### Web side (`assets/three_d_viewer.html`)

The page is loaded from app assets through `WebViewAssetLoader`:

- URL: `https://appassets.androidplatform.net/assets/three_d_viewer.html`

Inside page script:

1. Sets up scene, camera, renderer, lights, and orbit controls.
2. Creates fallback placeholder cube for visibility if model fails.
3. Loads `esp32.glb` via local asset URL.
4. Forces mesh materials visible and centers/scales model.
5. Smoothly interpolates world rotation toward target euler from bridge.
6. Supports zoom in/out and camera reset controls.

Vendor assets used:

- `assets/vendor/three.min.js`
- `assets/vendor/GLTFLoader.js`
- `assets/vendor/OrbitControls.js`

## 9) Dashboard behavior in detail

`MainDashboardFragment` observes `logHistory` and renders the latest point.

### Cards and metrics

- Battery card:
  - value text from `batteryPercentage`
  - if `batteryLife == "N/A"`, treats as plugged-in state
- CPU card:
  - progress indicator percent
  - color mapped green->red by load
- Temperature card:
  - Celsius or Fahrenheit according to user setting
  - dynamic line color based on source Celsius value
- Voltage and current cards:
  - fixed style line charts with filled region

### Chart policy

- Adds samples with elapsed seconds from ViewModel start time.
- Rejects non-monotonic x values.
- Visible x-range capped to latest 30 seconds.
- Temperature line recolors each sample based on temperature.

## 10) GPIO/I-O panel behavior in detail

`IoFragment` shows two synchronized representations:

1. immediate LED-style pin states around board image
2. short history chart per pin

### Dynamic pins observed

- GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO20, GPIO21, GPIO0, GPIO1, GPIO2, GPIO3, GPIO4

### Static rails

- 5V: forced HIGH
- GND: forced LOW
- 3.3V: forced HIGH

### History implementation

- Uses `ArrayDeque<Boolean>` per pin.
- Capacity: `HISTORY_SIZE = 36` samples.
- Each update appends latest state and drops oldest when full.
- Charts use stepped line mode to preserve digital shape.
- Point color is red for HIGH, gray for LOW.

## 11) Preferences and settings

There are two separate preference stores:

1. User display preference (`PreferenceManager`):
   - file: `esp32_prefs`
   - key: `is_fahrenheit` (default false)

2. Debug runtime preference (`MainActivity`):
   - file: `app_debug_prefs`
   - key: `use_mock_data` (default false)

`SettingsActivity` only controls Fahrenheit toggle and writes to `esp32_prefs`.

## 12) UI composition and resources

### Main activity layout (`activity_main.xml`)

- CoordinatorLayout root
- AppBarLayout with:
  - centered MaterialToolbar
  - fixed TabLayout
- ViewPager2 consuming main content
- extra hidden FrameLayout (`fragmentContainer`) currently unused

### Theme and palette

- Material3 day/night no action bar base
- custom dark-oriented color palette defined in `colors.xml`
- rounded component shapes (28dp and 20dp corner presets)

Note: `values-night/themes.xml` currently contains only a minimal stub style, while most actual color customization resides in `values/themes.xml` with dark background values.

## 13) Mock-data generator behavior

When enabled, `MainActivity` emits synthetic packets at fixed cadence:

- cadence: 250 ms
- IMU values: sine-wave short values with different amplitudes and phase offsets
- telemetry values:
  - temperature around 28 C with oscillation
  - voltage around 3.55 V with oscillation
  - current around 0.25 A with oscillation
  - battery and CPU with bounded sinusoidal variation
- GPIO values: square-wave toggles with pin-specific divisors
- all sensor connection flags forced connected (`1`)

This path is valuable for:

- UI development without hardware
- chart behavior validation
- orientation pipeline smoke tests

## 14) Logging and observability patterns

The app includes extensive staged debug logging in core paths:

- packet counters log first few events and periodic checkpoints
- socket connection state changes are logged
- unknown header bytes and oversized pending buffers produce warnings
- parse failures log error and reset parser state

Sensor connectivity transitions produce short toasts in main activity.

## 15) Known constraints, tradeoffs, and implementation quirks

1. Endpoint is hardcoded to single IP/port, no UI config.
2. Transport is cleartext WebSocket on LAN.
3. `logHistory` growth is unbounded.
4. Legacy/unused components remain in tree:
   - `LogFragment`
   - `ImuVisualizerView`
   - `MqttManager` stub
   - `bottom_nav_menu.xml`
5. Filament dependencies are included but current 3D path uses Three.js in WebView, not Filament.
6. Mixed XML and hardcoded strings exist (not fully centralized in `strings.xml`).
7. Some text labels in telemetry model are placeholders (for example network string set to "WebSocket").

## 16) Testing status

Current tests are default template-level only:

- Unit test: simple arithmetic assertion (`ExampleUnitTest`).
- Instrumented test: app package name assertion (`ExampleInstrumentedTest`).

There are no dedicated tests yet for:

- packet parsing edge cases (partial, malformed, misaligned frames)
- ViewModel history/state behavior
- IMU fusion math correctness
- fragment rendering logic
- mock generator invariants

## 17) Build, install, and run

From project root:

### Windows (PowerShell)

```powershell
./gradlew.bat clean assembleDebug
./gradlew.bat installDebug
```

### Unix/macOS shell

```bash
./gradlew clean assembleDebug
./gradlew installDebug
```

Useful tasks:

- `assembleDebug`: build debug APK
- `installDebug`: install on connected device/emulator
- `test`: run local unit tests
- `connectedAndroidTest`: run instrumented tests

## 18) Practical hardware/network expectations

To use live device mode successfully:

1. Phone must be on same network segment as ESP32 endpoint `192.168.4.1`.
2. ESP32 firmware must stream packets matching expected headers and byte layout.
3. Packet byte order must be little-endian as parser expects.
4. WebSocket endpoint must serve at `/` on port `3333`.

If any of these assumptions differ, the app may connect but show no meaningful updates.

## 19) Quick map of important files

- app entry and orchestration:
  - `app/src/main/java/com/example/esp32_diagtool/MainActivity.kt`
  - `app/src/main/java/com/example/esp32_diagtool/MainViewModel.kt`
- socket transport + parser:
  - `app/src/main/java/com/example/esp32_diagtool/network/SocketManager.kt`
- rendering fragments:
  - `app/src/main/java/com/example/esp32_diagtool/fragments/ThreeDFragment.kt`
  - `app/src/main/java/com/example/esp32_diagtool/fragments/MainDashboardFragment.kt`
  - `app/src/main/java/com/example/esp32_diagtool/fragments/IoFragment.kt`
- packet models:
  - `app/src/main/java/com/example/esp32_diagtool/model/ImuStreamPacket.kt`
  - `app/src/main/java/com/example/esp32_diagtool/model/TelemetryPacket.kt`
  - `app/src/main/java/com/example/esp32_diagtool/model/GpioPacket.kt`
- 3D web assets:
  - `app/src/main/assets/three_d_viewer.html`
  - `app/src/main/assets/esp32.glb`
  - `app/src/main/assets/vendor/`

## 20) Summary

This project is a real-time Android diagnostics frontend with a robust packet-ingestion core, a practical UI split by concern (3D orientation vs analog telemetry vs digital I/O), and an offline mock mode that mirrors live packet semantics.

Its strongest architectural decisions are:

- central shared-state fan-out through ViewModel/LiveData
- explicit packet contracts with byte-level parsing
- resilient streaming parser with partial-frame handling
- separate acquisition and rendering concerns

Its biggest current gaps are:

- hardcoded endpoint configuration
- limited automated testing
- some legacy/unused components not yet removed or integrated
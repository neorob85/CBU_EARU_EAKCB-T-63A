# EARU CBU EAKCB-T-63A — Smart Energy Meter Firmware

Firmware for the **EARU EAKCB-T-M (63 A)** single-phase DIN-rail smart energy meter, based on the **BK7231N** SoC (LibreTiny platform). The device measures electrical energy in real time and integrates with **Home Assistant** via MQTT.

- Provisioning Portal: 10.0.0.1:80
- Configuration Portal: <YOUR_IP>:8080


---

## Features

### Energy Metering
- Real-time measurement of voltage (V), current (A), active power (W), cumulative energy (kWh), and line frequency (Hz) via the **BL0942** metering IC over UART.
- Energy accumulation is offset-based and persisted to flash every hour, surviving power cycles.
- Energy counter can be reset remotely via MQTT command.

### Relay Control
- H-bridge relay (MD7620) driven by 150 ms pulses to avoid simultaneous bridge activation.
- Controllable locally (button short-press) or remotely (MQTT / Home Assistant).
- State is persisted across reboots.
- Red LED mirrors relay state.

### Temperature Monitoring
- NTC thermistor on ADC pin P23.
- Beta-equation calculation with fully configurable reference values (Vref, Rref, Tref, Beta, Rpull).

### WiFi & MQTT
- Auto-reconnect with exponential backoff.
- MQTT client (PubSubClient) with configurable broker, port, credentials, and buffer size.
- **Home Assistant MQTT Discovery**: entities are registered automatically on connect.
  - Switch: relay control
  - Sensors: voltage, current, power, energy, frequency, temperature
- Online/offline availability tracking via MQTT will-message.

### Provisioning Portal
- On first boot (or after WiFi reset) the device creates a soft-AP:
  - SSID: `EARU_CBU_EAKCB-M-T`
  - IP: `10.0.0.1`
- A captive web portal (port 80) lets you configure WiFi, MQTT, device name, serial number, and area.
- The portal scans nearby networks and pre-fills the form.
- WiFi credentials are tested before saving (35 s timeout); the soft-AP is disabled during the test to avoid channel conflicts on the BK7231N radio.

### Runtime Configuration Portal
- During normal operation a configuration web portal is available on port **8080** at the device's LAN IP.
- All settings can be changed without recompiling: WiFi, MQTT, device identity, NTC calibration, and energy publish interval.
- Password fields left blank preserve the existing credentials.

### OTA Firmware Updates
- HTTP POST-based OTA endpoint (`/update`) available both during provisioning (10.0.0.1) and normal operation (device LAN IP).
- Compatible with **curl** and with the ESPOTADASH web dashboard.

### Physical Button (P17)
| Hold duration | Action |
|---|---|
| Short press | Toggle relay |
| 5 s | Restart device |
| 10 s | Clear WiFi credentials, enter provisioning mode |
| 15 s+ | Factory reset (erase all configuration) |

Blue LED provides feedback for each hold zone.

### Status LED (Blue, P15)
| State | Pattern |
|---|---|
| Provisioning AP active | Blink every 1000 ms |
| Connecting to WiFi | Blink every 500 ms |
| WiFi connected, MQTT connecting | Blink every 250 ms |
| Fully connected | Solid on |
| WiFi lost | Off |
| Identify mode (30 s) | Blink every 100 ms |

---

## Hardware

| Component | Detail |
|---|---|
| SoC | BK7231N (LibreTiny `cbu` board) |
| Energy IC | BL0942 on Serial1 @ 4800 baud |
| Relay driver | MD7620 H-bridge (pins P24, P26) |
| Temperature | NTC thermistor on ADC P23 |
| Red LED | P9 — relay state |
| Blue LED | P15 — connectivity / identify |
| Button | P17 — multi-function |

---

## Configuration

### Compile-time (`.env` file)

Copy `.env.template` to `.env` and fill in the values before building.

| Variable | Default | Description |
|---|---|---|
| `CFG_FIRMWARE_VERSION` | `1.0.0` | Semantic firmware version |
| `CFG_DEVICE_VERSION` | `2024/03` | Hardware revision |
| `CFG_DEVICE_MANUFACTURER` | `Earu` | Manufacturer name |
| `CFG_DEVICE_MODEL` | `EAKCB-T-M (63 A)` | Model identifier |

### Runtime (via web portals)

| Parameter | Default | Description |
|---|---|---|
| `wifi_ssid` / `wifi_pass` | — | WiFi credentials |
| `mqtt_server` / `mqtt_port` | — / `1883` | MQTT broker |
| `mqtt_user` / `mqtt_pass` | — | MQTT credentials |
| `mqtt_buf` | `2048` | MQTT buffer size (bytes, min 256) |
| `device_name` | `EnergyMeter` | Name shown in Home Assistant |
| `device_sn` | `000000` | Serial number |
| `device_area` | — | Home Assistant area/room |
| `ha_prefix` | `homeassistant` | MQTT discovery prefix |
| `energy_interval` | `1000` | Publish interval (ms, min 100) |
| `dashboard_url` | — | ESPOTADASH server URL |
| `ntc_vref` / `ntc_rref` / `ntc_tref` | `3.3` / `47.0` / `25.0` | NTC reference V, R (kΩ), T (°C) |
| `ntc_beta` / `ntc_rpull` | `3950` / `200.0` | NTC Beta (K), pull-up R (kΩ) |

---

## Build & Flash

This project uses **PlatformIO** with three environments:

| Environment | Upload method | Target |
|---|---|---|
| `cbu` | Serial (USB) | Any |
| `cbu-ota` | OTA HTTP | Device at configured LAN IP (`192.168.1.250`) |
| `cbu-ota-prov` | OTA HTTP | Unconfigured device at provisioning AP (`10.0.0.1`) |

```bash
# Serial upload
pio run -e cbu -t upload

# OTA upload to device on LAN
pio run -e cbu-ota -t upload

# OTA upload during initial provisioning
pio run -e cbu-ota-prov -t upload
```

The `extra/load_env.py` script reads the `.env` file at build time and injects the four compile-time defines automatically.

---

## Libraries

| Library | Version | Purpose |
|---|---|---|
| PrefsManager | ^1.0.0 | Persistent key-value storage (LittleFS) |
| ESPOTADASH | ^1.2.0 | Web OTA dashboard |
| PubSubClient | ^2.8 | MQTT client |
| ArduinoJson | ^7.2.2 | JSON serialisation |
| NTC | ^1.0.0 | NTC thermistor calculation |
| HAmqttSTR | ^1.1.1 | Home Assistant MQTT entity framework |
| BL0942 | (local `lib/`) | BL0942 energy IC driver |

---

## Source Layout

```
src/
├── main.cpp          Core application (setup, loop, MQTT, relay)
├── main.h            Pin definitions and constants
├── config.h          ConfigData struct (runtime settings)
├── config_portal.h   Runtime config web portal (port 8080)
├── provisioning.h    Initial setup captive portal (port 80, AP mode)
├── settings.h        Firmware/hardware version constants
└── secrets.h         Legacy credentials placeholder (unused)
lib/
└── BL0942/src/       BL0942 energy metering IC driver
extra/
└── load_env.py       Build-time .env → compiler defines
platformio.ini        PlatformIO project configuration
.env.template         Compile-time configuration template
```

---

## Home Assistant Integration

Once the device is online and MQTT is configured, all entities are registered automatically via MQTT Discovery. No manual YAML configuration is required.

To reset discovery (e.g. after a rename), clear the retained MQTT topics under `<ha_prefix>/` for the device's unique ID, then restart the firmware.

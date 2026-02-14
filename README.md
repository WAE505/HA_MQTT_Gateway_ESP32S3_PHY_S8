# HA Bluetooth Proxy for Coded PHY BTHome

A standalone ESPHome project that receives **BLE 5.0 Coded PHY (S2/S8)** BTHome v2 advertisements on an ESP32-S3 and publishes them to Home Assistant via MQTT auto-discovery.

## Why?

The standard ESPHome Bluetooth Proxy cannot support Coded PHY — the entire HA bluetooth stack (`bluetooth_proxy` -> `NativeAPI` -> `Bleak` -> HA Bluetooth integration -> `BTHome-BLE`) lacks Coded PHY support at every layer. This project bypasses that pipeline entirely by receiving Coded PHY advertisements directly on the ESP32-S3 and publishing sensor data over MQTT.

## How It Works

```
BTHome Sensor (Coded PHY S8)
        │
        │ BLE 5.0 Extended Advertising
        ▼
ESP32-S3 (ble_ext_scanner)
        │
        │ Parse BTHome v2 payload
        ▼
ha_discovery component
        │
        │ MQTT auto-discovery + state
        ▼
Home Assistant (MQTT Integration)
        │
        ▼
Sensor entities (temperature, humidity, battery, RSSI, ...)
```

The `ble_ext_scanner` component uses the Bluedroid BLE 5.0 extended scanning API with a lock-free ring buffer to receive advertisements on both 1M and Coded PHY. The `ha_discovery` component parses BTHome v2 readings and publishes HA-compatible MQTT discovery configs and state updates.

## Prerequisites

- **Hardware**: ESP32-S3 board (tested with M5Stack Atom S3 Lite)
- **Home Assistant**: With Mosquitto MQTT broker add-on (or any MQTT broker)
- **Sensors**: BTHome v2 compatible sensors broadcasting on Coded PHY (e.g., ATC_ThermoBacon firmware)
- **ESPHome**: Installed locally or via HA add-on

## Setup

1. **Clone this repository:**
   ```bash
   git clone https://github.com/your-user/HA-Bluetooth-Proxy-ESP32S3-PHYS8.git
   cd HA-Bluetooth-Proxy-ESP32S3-PHYS8
   ```

2. **Create your secrets file:**
   ```bash
   cp esphome/secrets.yaml.example esphome/secrets.yaml
   ```

3. **Edit `esphome/secrets.yaml`** with your WiFi and MQTT credentials:
   ```yaml
   wifi_ssid: "MyWiFi"
   wifi_password: "MyPassword"
   mqtt_broker: "homeassistant.local"
   mqtt_port: "1883"
   mqtt_user: "mqtt"
   mqtt_password: "mqtt-password"
   ```

4. **Compile and flash:**
   ```bash
   esphome compile esphome/ha-ble-proxy.yaml
   esphome upload esphome/ha-ble-proxy.yaml
   ```

5. **Monitor logs:**
   ```bash
   esphome logs esphome/ha-ble-proxy.yaml
   ```

## Auto-Discovered Entities

Each BTHome device that broadcasts will appear in HA with these entities (depending on which data the sensor provides):

| Entity | Device Class | Unit | Category |
|--------|-------------|------|----------|
| Temperature | temperature | °C | — |
| Temp 60min avg | temperature | °C | diagnostic |
| Temp 4hr avg | temperature | °C | diagnostic |
| Humidity | humidity | % | — |
| Humidity 60min avg | humidity | % | diagnostic |
| Battery | battery | % | diagnostic |
| Compressor % | — | % | diagnostic |
| RSSI | signal_strength | dBm | diagnostic |

## Configuration

The main config is in `esphome/ha-ble-proxy.yaml`. Key parameters:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `scan_coded` | `true` | Enable Coded PHY scanning |
| `scan_1m` | `true` | Enable 1M PHY scanning |
| `scan_interval` | `1100` ms | BLE scan interval |
| `scan_window` | `1100` ms | BLE scan window (= interval for continuous) |
| `config_interval` | `300` s | Re-publish discovery configs interval |
| `state_interval` | `10` s | Min seconds between state publishes per device |

## Troubleshooting

**WiFi won't connect:**
- Check `secrets.yaml` credentials
- ESP32-S3 supports 2.4 GHz WiFi only

**MQTT not connecting:**
- Verify broker address and port in `secrets.yaml`
- Check that the MQTT user has publish permissions
- Verify broker is reachable from the ESP32-S3's network

**No BTHome sensors detected:**
- Ensure sensors are broadcasting BTHome v2 format (UUID `0xFCD2`)
- Check that Coded PHY is enabled on your sensors if using S8 firmware
- Monitor logs for "BTHome" parsing messages
- The scan window should match the scan interval for continuous scanning

**BLE and WiFi interference:**
- The ESP32-S3 shares the 2.4 GHz radio between BLE and WiFi
- BLE scan starts are deferred to boot priority -10 to let WiFi connect first
- If you see dropped connections, try reducing `scan_window` below `scan_interval`

**Entities not appearing in HA:**
- Verify MQTT discovery topic prefix matches your HA config (default: `homeassistant`)
- Check `mosquitto_sub -h broker -t "homeassistant/#" -v` for discovery messages
- Restart HA's MQTT integration after first flash

## Hardware

Tested on **M5Stack Atom S3 Lite** (ESP32-S3):
- Status LED: GPIO35
- Button: GPIO41 (hold 10s for reboot)
- USB-C for flashing and serial monitor (USB_SERIAL_JTAG)

## License

MIT

# simppeliTCU 🍃
**A lightweight, open-source DIY replacement for the Nissan Leaf ZE1 Telematics Control Unit (TCU).**

*Read this in Finnish: [README_FI.md](README_FI.md)*

With Nissan shutting down the NissanConnect EV services, `simppeliTCU` offers a local, Wi-Fi-based ESP32 solution to regain control of your car's climate and charging via the IT CAN bus (often referred to as CAR-CAN by the community). Similarly as the original TCU.

**Hardware used in this fork:** Waveshare **ESP32-S3-RS485-CAN**, with an optional **BK-7670 V1 breakout carrying a SIMCom A7670E** for cellular fallback. The wiring below describes this setup; the upstream project uses a LilyGO T-2CAN.

NOTE: This project is currently an draft implementation generated mainly with AI.

## Features
* 🌡️ **Remote Climate Control:** Start and active abort of the cabin heater/AC.
* 🔋 **Remote Charging:** Override the charge timer to start charging.
* 🔐 **Door Locking:** Lock and unlock doors remotely (disabled by default, enable via configuration).
* 📊 **Live Data:** Reads State of Charge (SOC %) and Cabin Temperature from the CAN bus.
* 🌐 **Web UI:** Simple, lightweight mobile-friendly web interface.
* 🏠 **Home Assistant Integration:** Automatic discovery of metrics and controls via MQTT.
* 📡 **MQTT Support:** Integrate with home automation or mobile apps over encrypted transport. Note: unless proper certificate validation is implemented, the MQTT server is not authenticated. See the [MQTT Setup Guide](docs/mqtt.md).

## Operation
* ⚙️ **Non-blocking operation:** The device processes CAN commands asynchronously. If a new command is issued while one is already in progress, it will be safely ignored.

![Screenshot of the Web UI from mobile phone browser](docs/screenshot.jpg)

## Videos
* **First trials:** [https://youtu.be/qn05-901b3Y](https://youtu.be/qn05-901b3Y)
* **Web interface through wifi:** [https://youtube.com/shorts/XqAgwizNodE](https://youtube.com/shorts/XqAgwizNodE)
* **MQTT use with IoTMQTTPanel and OVMS Connect:** [https://youtu.be/g8Yh6OgjL-Q?si=QtElrzG9FnF5YEUm](https://youtu.be/g8Yh6OgjL-Q?si=QtElrzG9FnF5YEUm)
* **Glove box removal (LHD ZE1 Leaf):** [https://youtu.be/kyx2A4U-M1M?si=YGewj1TeBEfrpOKV](https://youtu.be/kyx2A4U-M1M?si=YGewj1TeBEfrpOKV)

## Hardware Requirements

1. **Waveshare ESP32-S3-RS485-CAN**, with an onboard CAN transceiver.
2. Optional **AND BK-7670 V1 / SIMCom A7670E** modem, LTE antenna and active data SIM with the PIN request disabled.
3. For the modem, an **automotive-suitable regulated 5 V supply rated for at least 2 A**, including transmit current peaks.
4. A four-pin **SH1.0 cable**, modem header connections and connectors/pins to mate with the original TCU harness.

The [upstream Installation Guide](docs/INSTALLATION.md) covers the vehicle harness and LilyGO installation. For this fork, use the Waveshare connections below instead of the LilyGO pin assignments.

### ⚠️ CRITICAL WIRING WARNING

The Waveshare screw-terminal power input is rated **7–36 V**, so nominal 12 V connects there, using a fused, automotive-suitable supply. This input rating does not make every automotive transient safe. **Do not connect 12 V to the USB, 3V3, GPIO or modem VCC pins.** The original LilyGO board has different power requirements. [Waveshare hardware documentation](https://docs.waveshare.net/ESP32-S3-RS485-CAN/)

### Waveshare CAN wiring

Disconnect the original TCU's IT-CAN connection and connect the vehicle's **CAN-H to the Waveshare H terminal**, and **CAN-L to the L terminal**. The onboard CAN controller/transceiver uses **GPIO15 for TX and GPIO16 for RX**, as configured in `simppeliTCU/canInterface.cpp`. These are internal controller signals; the vehicle CAN pair connects to the **H/L terminals**, not directly to GPIO15/16. This fork does not use the upstream LilyGO GPIO7/6 assignments.

### A7670E modem wiring through SH1.0

Use the Waveshare's **four-pin GPIO SH1.0 connector**, not the two-pin RTC battery connector. GPIO1 and GPIO2 are assigned to UART1 by the LTE configuration:

<img src="docs/waveshare-sh10-connector.png" alt="Waveshare SH1.0 connector with black, red, orange and yellow wires next to the USB-C port" width="640">

*Photo of the Waveshare end of the cable used in this setup. From left to right at the white connector: black = GND, red = 3V3 (unused), orange = GPIO2 / ESP32 RX (to modem T), yellow = GPIO1 / ESP32 TX (to modem R). The modem end is not shown.*

| Waveshare SH1.0 signal | Wire in the cable used for this setup | BK-7670 V1 connection |
| --- | --- | --- |
| **GPIO1 — ESP32 TX** | **Yellow** | **R / RXD**, header pin 5 — modem input |
| **GPIO2 — ESP32 RX** | **Orange** | **T / TXD**, header pin 4 — modem output |
| **GND** | Black | **G / GND**, header pin 1 or 6, unless a common ground is already verified |
| **3V3** | Red | **Leave disconnected and insulate**; not the modem supply |

Cable colors can vary: identify each contact from the board's labels/pinout before connecting it. TX connects to RX in both directions. The BK-7670 breakout provides **3.3 V TTL UART**, compatible with ESP32 GPIO; this does not describe the bare modem module's pins. [Waveshare connector pinout](https://docs.waveshare.net/assets/images/ESP32-S3-RS485-CAN-details-17-962be6f963b528179bb59193896f4ac3.webp), [BK-A7670 manufacturer manual, page 4](https://img.yfisher.com/m0/1692268831380-a7670-series-model-bk-a7670-board-user-manual-v10.pdf#page=4)

Power the breakout's **V / VCC (header pin 2)** from the regulated **5 V / at least 2 A** supply, and its **G / GND** from the supply return. Do not use the SH1.0 red/3V3 wire to power it. The manufacturer recommends 5 V for this breakout. Leave **K/PWRKEY** and **S/SLEEP** unconnected for the documented default startup; the firmware does not control them. [BK-A7670 manufacturer manual](https://img.yfisher.com/m0/1692268831380-a7670-series-model-bk-a7670-board-user-manual-v10.pdf#page=4)

Example supply arrangement with a **non-isolated** step-down converter:

```text
Fused vehicle +12 V ----+---- Waveshare power terminal +
                       +---- Step-down IN+
Vehicle GND -----------+---- Waveshare power terminal -
                       +---- Step-down IN-

Step-down OUT+ (5 V) -------- BK-7670 V / VCC
Step-down OUT- (GND) -------- BK-7670 G / GND
```

UART requires a common logic ground. If the converter's IN- and OUT- are common, the shared supply return can provide it; verify continuity with power disconnected. Otherwise, connect the SH1.0 GND to modem GND. Connect the LTE antenna before powering the modem.

For this GPIO1/2 wiring, enter the following in the **USB serial CLI**, replacing `YOUR_APN` with your provider's APN:

```text
set lte_tx 1
set lte_rx 2
set lte_apn YOUR_APN
set lte_enabled true
reboot
```

Check `lte-status` and `mqtt-status` after reboot. Wi-Fi remains preferred; LTE initialization starts after 20 seconds without a Wi-Fi IP, or immediately if no STA network is configured. See the [4G setup and hardware test procedure](docs/lte.md) for detailed behavior and troubleshooting. Actual APNs, passwords and broker addresses stay in device NVS, not in this README or the firmware source.

## Configuration

The device configuration (Wi-Fi, MQTT credentials, etc.) is stored in persistent memory. You configure the system through the USB serial connection using a terminal (such as the Arduino IDE Serial Monitor, PuTTY, or similar) set to **115200 baud** with Newline (`\n`) or Carriage Return (`\r`) enabled.

Available commands:
* `list` - Shows all current configuration properties.
* `get <key>` - Prints the current value of the specified key.
* `set <key> <value>` - Updates the specified key with a new value (saved persistently).
* `lte-status` - Shows runtime LTE state, active uplink, last error and cached modem diagnostics.
* `mqtt-status` - Shows MQTT connection state, broker, active uplink and last connection result without reconnecting.
* `syslog-status` - Shows the UDP log destination, sender state and sent/dropped counters.
* `reboot` - Restarts the device to apply the changes immediately.
* `factory-reset` - Clears all configuration settings and restarts the device.

**Example configuration session:**
```text
set ssid MyWiFiNetwork
set password MySecretPassword
set fallback_ssid PhoneHotspot
set fallback_password PhonePassword
set ap_ssid LeafLocalAP
set ap_password LeafLocalPassword
set web_password MyWebPassword
set mqtt_server 192.168.1.100
set locking_enabled true
reboot
```

*Supported Configuration Keys:*
* `ssid`, `password` (For connecting to an existing Wi-Fi network / STA mode)
* `fallback_ssid`, `fallback_password` (Optional second STA network; an empty `fallback_ssid` disables fallback)
* `ap_ssid`, `ap_password` (For creating a local Wi-Fi Access Point / AP mode)
* `web_password` (HTTP Basic Authentication password for the Web UI; minimum 8 characters)
* `lte_enabled`, `lte_apn`, `lte_tx`, `lte_rx` (Optional UART/PPP cellular fallback; disabled by default. See [4G setup](docs/lte.md).)
* `syslog_enabled`, `syslog_host`, `syslog_port` (Optional UDP/RFC5424 remote logging; disabled by default, port 514)
* `hostName`
* `mqtt_server`, `mqtt_port`, `mqtt_user`, `mqtt_password`, `vehicle_id`
* `mqtt_tls` (Enable/disable TLS for MQTT connection; default: `true`)
* `locking_enabled` (Enable door lock/unlock functionality; default: `false`)
* `hass_discovery` (Enable/disable Home Assistant discovery messages; default: `true`)

**Wi-Fi Modes:**
* **STA Mode:** Active when `ssid` is configured. The device connects to your home/garage Wi-Fi.
  When `fallback_ssid` is configured, it tries the primary network first, then the fallback network after a 10-second attempt. While on fallback, it checks periodically for the primary network and switches back after detecting it twice at least five seconds apart. Wi-Fi settings take effect after `reboot`.
* **AP Mode:** Active when `ap_ssid` is configured. The device broadcasts its own Wi-Fi network (useful when away from home).
* **AP + STA Mode:** Active when both `ssid` and `ap_ssid` are configured. The device will connect to your home Wi-Fi while simultaneously broadcasting its own Access Point.

**Web UI access:** Set `web_password` via USB serial CLI before opening the Web UI. The browser will ask for username `admin` and the configured password. All Web UI controls require authentication. If `web_password` is empty (including after a factory reset), Web UI access is disabled; `set web_password` without a value disables it again. HTTP Basic Authentication does not encrypt credentials, so use the Web UI only on a trusted network and do not expose port 80 to the Internet.

**4G fallback:** With LTE enabled and configured, primary Wi-Fi remains preferred, followed by fallback Wi-Fi, then the modem. The first modem attempt starts after 20 seconds without a Wi-Fi IP (immediately if no STA network is configured). An established PPP session stays on standby; Wi-Fi must be connected for 10 seconds before switching back from LTE. MQTT reconnects on the selected interface using the same broker, credentials and topics. See [4G setup and hardware test procedure](docs/lte.md).

**Remote configuration and logging:** Wi-Fi, LTE and syslog settings can be staged over MQTT and saved with a separate apply/reboot request. Passwords are write-only over MQTT. See [MQTT configuration and syslog-ng setup](docs/remote-configuration.md) for topics, JSON examples and the session/replay protection.

## Compiling and Uploading

For instructions on how to compile and upload the firmware using the Arduino IDE or the Command Line Interface (CLI), please refer to the [Compiling and Uploading Guide](docs/compiling.md).

## Flashing Pre-compiled Binaries

For instructions on downloading and flashing pre-compiled release binaries (using GUI tools or `esptool`), please refer to the [Pre-compiled Binaries Flashing Guide](docs/flashing.md).

## The CAN communication
Please see the [CAN Communication Details](docs/can.md) document for information regarding CAN bus messages, commands, known issues, and limitations related to the IT-CAN integration.

## License & Disclaimer (MIT License)
This project is licensed under the MIT License. 

**DISCLAIMER OF LIABILITY:**
This software and hardware modification interacts with the high-voltage control systems of a vehicle. It is provided "AS IS", without warranty of any kind, express or implied. By using this code, you assume all risks. The authors or contributors will NOT be held liable for any damage to your vehicle, voided warranties, property damage, or personal injury resulting from the use of this software or hardware. **USE AT YOUR OWN RISK.**

## Credits
* Inspired by the incredible work of the [OVMS Project](https://github.com/openvehicles/Open-Vehicle-Monitoring-System-3) and [Dalathegreat](https://github.com/dalathegreat) (Dala's EV Repair) for documenting the Nissan Leaf CAN buses.

# UART/PPP 4G fallback

This implementation targets the SIMCom A7670E on the [AND BK-A7670E breakout](https://www.and-global.com/list_23/568.html), using standard PPP AT commands over UART1 at 115200 baud. Confirm the marking on the actual modem: the linked product is **A7670E**, not A7676E. Firmware compilation and automatic network selection tests have passed; UART communication, registration and MQTT failover still need verification on the physical modem.

## Hardware preparation

Check the pin labels and electrical requirements for your exact breakout before connecting it. The breakout supply and UART logic voltage are separate requirements; bare SIMCom module pins must not be assumed to accept ESP32 logic or the breakout supply voltage.

* Power the modem from a regulated supply rated for its transmit current peaks, with the LTE antenna connected. Do not power it from an ESP32 GPIO or assume the ESP32's 3.3 V regulator is sufficient.
* Connect a common ground between the modem supply, modem and ESP32.
* Connect ESP32 TX to modem RX, and ESP32 RX to modem TX, using level conversion if required by the breakout.
* `lte_tx` and `lte_rx` refer to **ESP32 GPIO numbers**, not modem pin numbers. No modem GPIO is selected by default. Choose exposed, otherwise unused pins from your Waveshare board's pinout.
* The firmware reserves CAN GPIO15/16, USB GPIO19/20, flash/PSRAM GPIO26..37, UART0 GPIO43/44 and boot strapping pins. It also rejects identical TX and RX assignments at startup.
* The modem must be powered on and respond to `AT` before PPP initialization can succeed. Use the breakout's documented automatic startup or power button. This version does not drive PWRKEY/reset or use the modem USB port.
* Activate the SIM and data service, and disable its SIM PIN request in a phone first. The firmware does not send or guess SIM PINs.

## CLI configuration

The existing USB CLI/NVS configuration is used. Replace both GPIO placeholders with the actual wiring before entering these commands:

```text
set lte_apn YOUR_APN
set lte_tx <ESP32_TX_GPIO>
set lte_rx <ESP32_RX_GPIO>
set lte_enabled true
reboot
```

Replace `YOUR_APN` with the APN supplied by your mobile operator for your subscription. This version supports APNs without additional PAP/CHAP username/password authentication. APN, enable flag and UART changes take effect after reboot. `list` and `get lte_apn`, `get lte_tx`, `get lte_rx`, `get lte_enabled` show the stored configuration. Actual APNs and connection credentials are stored in the device's NVS, not in source files.

Keep the existing `ssid`/`password`, optional `fallback_ssid`/`fallback_password`, and all MQTT settings. Your MQTT broker must be reachable from the mobile network on its configured port. A LAN-only broker address cannot be reached directly over cellular.

Disable cellular fallback with:

```text
set lte_enabled false
reboot
```

## Selection and retry behavior

Use `lte-status` in the USB serial CLI to inspect the runtime state, modem task, active uplink, boot APN/UART settings, attempt count, last mobile attachment, cached CSQ signal, PPP IP and last error. The command prints a thread-safe snapshot without issuing AT commands. Signal/attachment are sampled during registration; they are not live measurements in PPP data mode. Pending CLI configuration changes are flagged as requiring a reboot. An initialization error still cannot distinguish AT synchronization, SIM PIN or driver failure by itself.

Use `mqtt-status` to inspect the broker connection separately: it reports the broker/port, TLS mode, selected uplink, current client state, topic prefix, connection attempt count and last connection result. It does not reconnect or publish and does not print passwords. A PPP IP alone does not mean that MQTT is connected. Result `0` is connected; `-4` is a connection timeout, `-3` a lost connection, `-2` a TCP/TLS connection failure, and `-1` disconnected. Positive results `1`..`5` are broker rejections (protocol, client ID, unavailable, credentials, authorization).

1. The existing Wi-Fi manager tries primary Wi-Fi first and then the configured fallback Wi-Fi.
2. After 20 continuous seconds without a Wi-Fi IP, a background task initializes the modem and waits for mobile registration. Without a configured STA SSID, the first modem attempt starts immediately.
3. When PPP has an IP, 4G becomes the default route. MQTT closes its old sockets, reconnects, resubscribes and republishes discovery/known metrics through its existing reconnect path.
4. When Wi-Fi returns while LTE is working, it must remain connected for 10 seconds before taking over. If LTE fails during that period, an available Wi-Fi connection can take over immediately.
5. Once established, PPP remains connected on standby while Wi-Fi is preferred. This is routing failover, not modem power management; the powered modem still consumes power and may generate cellular control traffic. On a later Wi-Fi loss, an already healthy PPP link can take over immediately.
6. Failed attempts wait at least 30 seconds before retrying. Registration has a 120-second window, and PPP IP negotiation has a 60-second window. Missing modem, antenna, SIM service or coverage leaves the device offline and retrying, with CAN and serial processing continuing.

Only a Wi-Fi STA connection with an IP counts as Wi-Fi connectivity. A local AP alone does not prevent LTE fallback. A Wi-Fi network with no Internet still counts as connected: this implementation does not probe Internet reachability or switch links solely because the MQTT broker is unavailable.

The background task owns the modem and route selection; modem AT waits do not run in the main CAN loop. MQTT retains its existing synchronous TCP/TLS connection implementation, which can still wait for network timeouts. The change does not make all network operations asynchronous.

The existing Web UI and password protection are retained. This feature does not provide mobile-network access to the Web UI, a VPN, passenger Internet sharing or NAT. Mobile carrier addressing may prevent inbound connections; MQTT uses an outbound connection.

## First hardware test

Monitor at 115200 baud. Do the initial tests on the bench before relying on remote car control.

1. Start with Wi-Fi available. Verify `[Network] Uplink: WiFi` and the normal MQTT status/metrics on the broker.
2. Disable both configured Wi-Fi networks. Look for `[LTE] No WiFi; starting modem`, `[LTE] Registered; starting PPP data connection`, `[LTE] PPP connected, IP: ...`, `[Network] Uplink: 4G`, then `[MQTT] Connected` (the two IP/uplink logs may appear in either order).
3. Verify that new metrics and command responses reach your MQTT broker over cellular. Check this from a separate broker client; a printed PPP IP alone does not prove MQTT connectivity.
4. Restore Wi-Fi. Verify a switch to Wi-Fi after 10 seconds of stability and a fresh MQTT connection.
5. Repeat with primary absent but fallback Wi-Fi available, neither link available, a disconnected/unpowered modem, and LTE disabled. Confirm CAN reception and CLI responsiveness during failed registration and retries.

The host tests cover startup priority, offline recovery, modem startup grace, Wi-Fi stability/flapping, standby handover and `millis()` rollover. They do not emulate the modem or replace these hardware tests.

References: [Espressif PPP example](https://github.com/espressif/arduino-esp32/tree/3.3.12/libraries/PPP/examples/PPP_Basic), [esp_modem modes and A76xx CMUX limitations](https://docs.espressif.com/projects/esp-protocols/esp_modem/docs/latest/README.html).

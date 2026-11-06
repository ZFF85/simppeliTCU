# MQTT network configuration

The firmware accepts Wi-Fi and LTE configuration over the existing MQTT broker. It uses the same configuration setters and NVS namespace as the USB CLI. No extra Arduino library is required: JSON support is bundled with the ESP32 core.

Topic templates, using placeholders for your MQTT username and vehicle ID:

| Purpose | Topic |
| --- | --- |
| JSON request | `ovms/USERNAME/VEHICLE_ID/config/request` |
| JSON response | `ovms/USERNAME/VEHICLE_ID/config/response` |

Replace `USERNAME` and `VEHICLE_ID` with your device configuration. Subscribe to the response topic **before** publishing a request. Requests and responses must not be retained. Responses are published with retain disabled. All addresses, credentials and identifiers below are examples/placeholders, not deployed configuration. Set actual values over USB or MQTT; they are stored on the device, not compiled into the firmware. Keep any personal command notes or configuration exports in the Git-ignored root `local/` directory.

## Read current/staged configuration

Publish this JSON to the request topic using MQTT Explorer, an MQTT client or Home Assistant's `mqtt.publish` action:

```json
{"action":"get"}
```

The response includes `ok`, `message`, `pending`, `settings`, the selected `uplink`, `mqtt_connected` and a 32-character `session`. Passwords are omitted; only `password_set` and `fallback_password_set` flags are returned. When `pending` is true, `settings` describes the staged configuration, not the still-running one.

## Stage settings

Copy the current `session` value from the GET response into your SET request. For example:

```json
{
  "action": "set",
  "session": "COPY_THE_32_CHARACTER_SESSION_HERE",
  "settings": {
    "ssid": "HomeWiFi",
    "password": "NewHomePassword",
    "fallback_ssid": "PhoneHotspot",
    "fallback_password": "PhonePassword",
    "lte_enabled": true,
    "lte_apn": "YOUR_APN",
    "lte_tx": 1,
    "lte_rx": 2
  }
}
```

Only include fields you want to change. Subsequent SET requests merge into the staged configuration. Values are case-sensitive, including passwords; booleans must be JSON `true`/`false`, and GPIOs must be integers. An empty `fallback_ssid` disables fallback Wi-Fi. `lte_enabled: false` disables LTE after apply/reboot.

The allowlist is exactly the eight settings shown above. MQTT broker credentials, web passwords, factory reset, arbitrary CLI execution and modem AT commands are not exposed through this interface. Invalid fields/types, duplicate JSON keys, oversized strings, reserved GPIOs and deeply nested requests are rejected. Requests are limited to 1024 bytes.

SET only stages changes in RAM. It does **not** write NVS, alter the active Wi-Fi connection or restart the modem. A reboot before APPLY discards the staged changes. Avoid mixing USB CLI writes with a pending remote transaction, because APPLY saves the complete staged snapshot.

## Save and apply

After checking the GET response, publish:

```json
{"action":"apply","session":"COPY_THE_32_CHARACTER_SESSION_HERE"}
```

The firmware validates the combined settings, acknowledges the request, waits at least one second and saves/reboots when the CAN sequence is idle. If a CAN sequence is already active when APPLY arrives, the response says busy; retry when idle. A successful reconnect followed by GET verifies the stored values.

APPLY rejects enabled LTE with missing APN, unset/reserved/identical GPIOs, fallback Wi-Fi without a primary SSID, or a configuration with no primary Wi-Fi and LTE disabled. These checks cannot verify that credentials are correct or that a network will be reachable. Keep a working fallback path when changing the primary network; USB CLI remains the recovery method if remote connectivity is lost.

The session expires after ten minutes and changes on every MQTT reconnect. Read GET again after expiration/reconnection. It prevents old retained/replayed SET/APPLY messages from reapplying settings or causing a reboot loop; it is **not** authentication. Protect these topics with the broker's credentials and per-device ACLs. The existing MQTT transport is used, including its configured TLS behavior. No passwords or raw configuration request payloads are written to serial or included in responses.

Multi-key NVS updates use the existing setters and are not atomic across power loss. Keep the supply stable while APPLY saves and restarts.

## Verification

Automated host tests cover malformed/duplicate JSON, input limits, strict value types, protected GPIOs, combined-setting validation, session expiration/reconnect rotation, password redaction, staging without writes and CAN-idle apply. The ESP32-S3 firmware is compile-checked.

On hardware: GET, stage one harmless change, GET to inspect it, then APPLY and verify reconnect; test an expired session and invalid GPIO while switching Wi-Fi/4G. These broker/modem checks have not been run automatically against your live services.

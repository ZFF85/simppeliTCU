# Compiling and Flashing Firmware

## Compiling and Uploading with Arduino IDE

This fork defaults to **Waveshare ESP32-S3-RS485-CAN**. The upstream compile-time board selection is retained in `simppeliTCU/hardware.h`; LilyGO must be selected explicitly. For either ESP32-S3 board, configure the Arduino IDE as follows:

1. **Board Selection**: Select **ESP32S3 Dev Module** as your target board.
   * If you haven't set up the ESP32 core yet, please follow the [LilyGO T-2CAN Quick Start Guide](https://wiki.lilygo.cc/get_started/en/High_speed/T-2Can/T-2Can.html#Quick-Start) to download the required board files.
   
   ![Select ESP32S3 Dev Module](arduino_ide_esp32s3_dev_module.jpg)

2. **Serial Output Configuration**: **USB CDC On Boot** needs to be set to **"Enabled"** for the printouts to appear in the Serial Monitor.
   
   ![Enable USB CDC on Boot](set-cdc-on-boot-enabled.jpg)

## Compiling and Uploading with Arduino CLI

If you prefer using the command line, you can build and flash the firmware using the [Arduino CLI](https://arduino.github.io/arduino-cli/).

1. **Install the ESP32 core and required libraries**:
   The UART/PPP LTE implementation is compiled and tested with ESP32 Arduino core **3.3.12**. It uses the core's bundled `PPP`, `Network` and `NetworkClientSecure` libraries; no separate modem library is required.
   ```bash
   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli lib install PubSubClient
   ```

   Only LilyGO dual-CAN builds also require `arduino-cli lib install autowp-mcp2515`. Waveshare builds disable the MCP2515 interface automatically.

2. **Compile the sketch for Waveshare** (USB CDC On Boot enabled, optionally embedding the current git commit hash):
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc \
     --build-path /tmp/simppeli-tcu-flash \
     --build-property "compiler.cpp.extra_flags=-DBOARD=WAVESHARE_ESP32_S3_RS485_CAN -DGIT_HASH_RAW=$(git rev-parse --short HEAD)" \
     simppeliTCU
   ```
   *(If compiled in Arduino IDE or without `GIT_HASH_RAW`, the version string in `simppeliTCU/version.h` defaults to showing `(local)` as the commit hash.)*

   For LilyGO, replace `WAVESHARE_ESP32_S3_RS485_CAN` with `LILYGO_T2CAN`. The firmware pins and second-CAN availability follow this setting. LTE UART pins must not overlap either board's CAN/SPI wiring.

3. **Upload to the board** (replace `<PORT>` with your COM port, e.g., `COM3` or `/dev/cu.usbserial...`):
   ```bash
   arduino-cli upload -p <PORT> --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc \
     --build-path /tmp/simppeli-tcu-flash simppeliTCU
   ```

## Running Unit Tests

### Prerequisites

- CMake 3.14+
- MinGW or Visual Studio (Windows)
- Git (for FetchContent to download Google Test)
- cJSON development files and pkg-config (optional; if unavailable, CMake downloads the pinned cJSON source for the remote configuration tests). Firmware builds use the cJSON bundled with the ESP32 core.

### Commands

```bash
# Navigate to test directory
cd test

# Create and enter build directory
mkdir build
cd build

# Configure with MinGW
cmake -G "MinGW Makefiles" ..

# Build the tests
cmake --build .

# Run all tests
ctest
```

The network selection tests can also run without Google Test or Arduino:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -IsimppeliTCU test/test_uplinkPolicy.cpp -o /tmp/simppeli-uplink-policy-tests
/tmp/simppeli-uplink-policy-tests
```

## Creating a New Release

Releases are created automatically by GitHub Actions when the version is bumped on the `main` branch:

1. Update [`CHANGELOG.md`](../CHANGELOG.md): move or add the changes under a new version heading (e.g., `## [v0.1.1] - YYYY-MM-DD`).
2. Update [`simppeliTCU/version.h`](../simppeliTCU/version.h): bump `FW_VERSION` to the matching version string (e.g., `#define FW_VERSION "v0.1.1"`).
3. Commit and push/merge the changes to `main`.

When the workflow runs on `main` and detects that a git tag for `FW_VERSION` does not yet exist, it automatically:
- Embeds the short git commit hash into the firmware binaries.
- Creates and pushes the annotated git tag (e.g., `v0.1.1`).
- Extracts the matching release notes from `CHANGELOG.md`.
- Publishes a **GitHub Release** with the compiled firmware binaries (`.bin` and `.zip`) for all supported boards.

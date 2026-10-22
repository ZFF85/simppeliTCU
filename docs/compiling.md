# Compiling and Flashing Firmware

## Compiling and Uploading with Arduino IDE

To compile and upload the firmware for the Lilygo T-2CAN board using the Arduino IDE, please configure the following settings:

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

2. **Compile the sketch** (ensuring USB CDC On Boot is enabled):
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc simppeliTCU
   ```

3. **Upload to the board** (replace `<PORT>` with your COM port, e.g., `COM3` or `/dev/cu.usbserial...`):
   ```bash
   arduino-cli upload -p <PORT> --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc simppeliTCU
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



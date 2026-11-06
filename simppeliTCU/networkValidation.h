#pragma once

#include <ctype.h>
#include "hardware.h"

// Exclude ESP32-S3 flash/PSRAM, USB, UART0 and strapping pins, plus the
// selected board's CAN/SPI wiring so UART cannot take over those signals.
inline bool validModemGpio(int pin) {
    if (pin == CAN_TX_PIN || pin == CAN_RX_PIN) return false;
#if HAS_SECOND_CAN
    if (pin == MCP2515_CS || pin == MCP2515_SCLK || pin == MCP2515_MOSI ||
        pin == MCP2515_MISO || pin == MCP2515_RST || pin == MCP2515_INT) return false;
#endif
    return pin == -1 || pin == 1 || pin == 2 ||
           (pin >= 4 && pin <= 18) || pin == 21 ||
           (pin >= 38 && pin <= 42) || pin == 47 || pin == 48;
}

inline bool validNetworkName(const char* value, bool apn = false) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (!isalnum(*p) && *p != '.' && *p != '-' && (!apn || *p != '_')) return false;
    }
    return true;
}

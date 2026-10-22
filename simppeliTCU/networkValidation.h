#pragma once

#include <ctype.h>

// Exclude ESP32-S3 flash/PSRAM, USB, Waveshare CAN, UART0 and strapping pins.
inline bool validModemGpio(int pin) {
    return pin == -1 || pin == 1 || pin == 2 ||
           (pin >= 4 && pin <= 14) || pin == 17 || pin == 18 || pin == 21 ||
           (pin >= 38 && pin <= 42) || pin == 47 || pin == 48;
}

inline bool validNetworkName(const char* value, bool apn = false) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (!isalnum(*p) && *p != '.' && *p != '-' && (!apn || *p != '_')) return false;
    }
    return true;
}

#pragma once

#include <stddef.h>
#include <stdio.h>

inline size_t formatSyslog(char* output, size_t capacity, const char* hostname, const char* message) {
    if (!capacity) return 0;
    // RFC5424: local0.info, NILVALUE timestamp/PROCID/MSGID/structured data.
    int count = snprintf(output, capacity, "<134>1 - %s simppeliTCU - - - %s", hostname, message);
    if (count < 0) { output[0] = '\0'; return 0; }
    size_t size = static_cast<size_t>(count) < capacity ? static_cast<size_t>(count) : capacity - 1;
    for (size_t i = 0; i < size; ++i) {
        if (static_cast<unsigned char>(output[i]) < 32 || output[i] == 127) output[i] = ' ';
    }
    return size;
}

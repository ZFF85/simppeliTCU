#pragma once

void setupSystemLog();
// Bounded, nonblocking enqueue; do not pass passwords or raw config payloads.
#if defined(__GNUC__)
void systemLog(const char* format, ...) __attribute__((format(printf, 1, 2)));
#else
void systemLog(const char* format, ...);
#endif
void printSyslogStatus();

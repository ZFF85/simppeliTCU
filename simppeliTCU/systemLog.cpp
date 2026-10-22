#include "systemLog.h"
#include "syslogFormat.h"
#include "configuration.h"
#include "connectivity.h"
#include <NetworkUdp.h>
#include <Network.h>
#include <atomic>
#include <stdarg.h>

namespace {
struct LogMessage { uint32_t uptime; char text[240]; };
QueueHandle_t logQueue = nullptr;
char host[64];
char hostname[16];
uint16_t port;
bool enabledAtBoot;
std::atomic<uint32_t> sent{0};
std::atomic<uint32_t> dropped{0};
std::atomic<const char*> lastResult{"not started"};

void syslogTask(void*) {
    NetworkUDP udp;
    IPAddress address;
    bool resolved = false;
    uint32_t resolvedAt = 0;
    Uplink previous = Uplink::OFFLINE;
    LogMessage message;
    for (;;) {
        if (xQueueReceive(logQueue, &message, portMAX_DELAY) != pdTRUE) continue;
        // Hold one entry while offline; the bounded queue absorbs a short outage.
        while (getActiveUplink() == Uplink::OFFLINE) {
            lastResult.store("waiting for network");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        Uplink uplink = getActiveUplink();
        if (uplink != previous) {
            udp.stop();
            resolved = false;
            previous = uplink;
        }
        if (!resolved || uint32_t(millis() - resolvedAt) >= 300000) {
            resolved = address.fromString(host) || Network.hostByName(host, address) == 1;
            if (!resolved) {
                lastResult.store("DNS resolution failed");
                ++dropped;
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }
            resolvedAt = millis();
        }
        char text[288];
        snprintf(text, sizeof(text), "[uptime_ms=%lu] %s", static_cast<unsigned long>(message.uptime), message.text);
        char packet[384];
        size_t length = formatSyslog(packet, sizeof(packet), hostname, text);
        bool ok = udp.beginPacket(address, port) &&
                  udp.write(reinterpret_cast<const uint8_t*>(packet), length) == length && udp.endPacket();
        if (ok) {
            ++sent;
            lastResult.store("UDP sent (server receipt not confirmed)");
        } else {
            ++dropped;
            lastResult.store("UDP send failed");
            udp.stop();
            resolved = false;
        }
    }
}
} // namespace

void setupSystemLog() {
    enabledAtBoot = getSyslogEnabled();
    snprintf(host, sizeof(host), "%s", getSyslogHost());
    snprintf(hostname, sizeof(hostname), "%s", getHostName());
    port = getSyslogPort();
    if (!enabledAtBoot) { lastResult.store("disabled"); return; }
    if (!host[0]) { lastResult.store("host not configured"); return; }
    logQueue = xQueueCreate(16, sizeof(LogMessage));
    if (!logQueue) { lastResult.store("queue allocation failed"); return; }
    if (xTaskCreate(syslogTask, "syslog", 4096, nullptr, 1, nullptr) != pdPASS) {
        vQueueDelete(logQueue);
        logQueue = nullptr;
        lastResult.store("task creation failed");
        return;
    }
    lastResult.store("ready");
    systemLog("[System] Boot; syslog forwarding enabled");
}

void systemLog(const char* format, ...) {
    LogMessage message;
    message.uptime = millis();
    va_list args;
    va_start(args, format);
    vsnprintf(message.text, sizeof(message.text), format, args);
    va_end(args);
    Serial.println(message.text);
    if (logQueue && xQueueSend(logQueue, &message, 0) != pdTRUE) ++dropped;
}

void printSyslogStatus() {
    Serial.println("Syslog status:");
    Serial.printf("  Enabled at boot: %s\n", enabledAtBoot ? "yes" : "no");
    Serial.printf("  Server: %s:%u (UDP, RFC5424)\n", host[0] ? host : "not configured", port);
    Serial.printf("  Worker: %s\n", logQueue ? "running" : "not running");
    Serial.printf("  Queued: %u\n", logQueue ? static_cast<unsigned>(uxQueueMessagesWaiting(logQueue)) : 0);
    Serial.printf("  Sent: %lu; dropped: %lu\n", static_cast<unsigned long>(sent.load()), static_cast<unsigned long>(dropped.load()));
    Serial.printf("  Last result: %s\n", lastResult.load());
    if (enabledAtBoot != getSyslogEnabled() || strcmp(host, getSyslogHost()) || port != getSyslogPort()) {
        Serial.println("  Stored syslog settings differ from runtime; reboot to apply.");
    }
}

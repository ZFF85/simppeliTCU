#include "connectivity.h"
#include "configuration.h"
#include "systemLog.h"
#include <WiFi.h>
#include <PPP.h>
#include <atomic>
#include <lwip/dns.h>
#include <lwip/tcpip.h>

#if !CONFIG_LWIP_PPP_SUPPORT || !ARDUINO_HAS_ESP_MODEM
#error "LTE fallback requires an ESP32 Arduino core with PPP/esp_modem support (tested with 3.3.12)."
#endif

#if !CONFIG_LWIP_TCPIP_CORE_LOCKING
#error "LTE DNS failover requires an ESP32 core with TCP/IP core locking (tested with 3.3.12)."
#endif

namespace {
std::atomic<bool> taskRunning{false};
std::atomic<bool> pppHasIp{false};
std::atomic<Uplink> activeUplink{Uplink::OFFLINE};

// Snapshot at boot: CLI updates take effect after reboot and cannot race AT commands.
char apn[64];
int txPin;
int rxPin;
bool wifiConfigured;

enum class ModemState { DISABLED_BY_CONFIG, CONFIG_ERROR, WAIT_WIFI, INITIALIZING,
                        WAIT_REGISTRATION, CONNECTING, ONLINE, RECOVERING, RETRY_WAIT };
struct ModemStatus {
    ModemState state = ModemState::DISABLED_BY_CONFIG;
    bool enabledAtBoot = false;
    bool registered = false;
    int signal = -1;
    uint32_t signalSampleAt = 0;
    uint32_t stateSince = 0;
    uint32_t attempts = 0;
    const char* lastError = "none";
    char ip[16] = {};
};
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
ModemStatus modemStatus;

void setModemState(ModemState state, const char* error = nullptr) {
    uint32_t now = millis();
    portENTER_CRITICAL(&statusMux);
    if (modemStatus.state != state) {
        modemStatus.state = state;
        modemStatus.stateSince = now;
    }
    if (error) modemStatus.lastError = error;
    portEXIT_CRITICAL(&statusMux);
}

const char* modemStateName(ModemState state) {
    switch (state) {
        case ModemState::DISABLED_BY_CONFIG: return "disabled";
        case ModemState::CONFIG_ERROR: return "configuration/task error";
        case ModemState::WAIT_WIFI: return "waiting (WiFi preferred / startup grace)";
        case ModemState::INITIALIZING: return "initializing modem";
        case ModemState::WAIT_REGISTRATION: return "waiting for mobile registration";
        case ModemState::CONNECTING: return "negotiating PPP/IP";
        case ModemState::ONLINE: return "PPP online (active or standby)";
        case ModemState::RECOVERING: return "recovering modem";
        case ModemState::RETRY_WAIT: return "waiting to retry (or for WiFi loss)";
    }
    return "unknown";
}

// lwIP DNS is global in the precompiled Arduino core. Keep the resolvers
// learned from DHCP/PPP separately, including when the standby link renews.
portMUX_TYPE dnsMux = portMUX_INITIALIZER_UNLOCKED;
ip_addr_t savedDns[2][DNS_MAX_SERVERS] = {};
bool dnsKnown[2] = {false, false};
bool dnsChanged = false;

void rememberDns(unsigned index) {
    ip_addr_t servers[DNS_MAX_SERVERS];
    LOCK_TCPIP_CORE();
    for (unsigned i = 0; i < DNS_MAX_SERVERS; ++i) servers[i] = *dns_getserver(i);
    UNLOCK_TCPIP_CORE();
    portENTER_CRITICAL(&dnsMux);
    memcpy(savedDns[index], servers, sizeof(servers));
    dnsKnown[index] = true;
    dnsChanged = true;
    portEXIT_CRITICAL(&dnsMux);
}

void restoreDns(Uplink selected, bool switched) {
    if (selected == Uplink::OFFLINE) return;
    unsigned index = selected == Uplink::WIFI ? 0 : 1;
    ip_addr_t servers[DNS_MAX_SERVERS];
    portENTER_CRITICAL(&dnsMux);
    bool restore = dnsKnown[index] && (switched || dnsChanged);
    if (restore) {
        memcpy(servers, savedDns[index], sizeof(servers));
        dnsChanged = false;
    }
    portEXIT_CRITICAL(&dnsMux);
    if (restore) {
        LOCK_TCPIP_CORE();
        for (unsigned i = 0; i < DNS_MAX_SERVERS; ++i) dns_setserver(i, &servers[i]);
        dns_clear_cache();
        UNLOCK_TCPIP_CORE();
    }
}

bool wifiReady() {
    return WiFi.status() == WL_CONNECTED && WiFi.STA.hasIP();
}

void onModemEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            rememberDns(0);
            break;
        case ARDUINO_EVENT_PPP_GOT_IP:
            rememberDns(1);
            pppHasIp.store(true);
            break;
        case ARDUINO_EVENT_PPP_LOST_IP:
        case ARDUINO_EVENT_PPP_DISCONNECTED:
        case ARDUINO_EVENT_PPP_STOP:
            pppHasIp.store(false);
            break;
        default:
            break;
    }
}

void selectUplink(Uplink selected) {
    NetworkInterface* interface = nullptr;
    if (selected == Uplink::WIFI) interface = &WiFi.STA;
    if (selected == Uplink::LTE) interface = &PPP;
    if (interface && !interface->isDefault() && !interface->setDefault()) {
        selected = Uplink::OFFLINE;
    }
    Uplink previous = activeUplink.load();
    restoreDns(selected, selected != previous);
    activeUplink.store(selected);
    if (selected != previous) {
        systemLog("[Network] Uplink: %s", selected == Uplink::WIFI ? "WiFi" :
                      selected == Uplink::LTE ? "4G" : "offline");
    }
}

// Only this task calls PPP methods. Modem AT calls may wait for seconds; never
// call them from loop(), where CAN sequences and the serial CLI are serviced.
void modemTask(void*) {
    UplinkPolicy policy(wifiConfigured ? 20000 : 0);
    bool initialized = false;
    bool dataMode = false;
    bool hadIp = false;
    bool attempted = false;
    uint32_t lastAttempt = 0;
    uint32_t stageStarted = 0;
    uint32_t lastAttachPoll = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        uint32_t now = millis();
        bool lteReady = initialized && PPP.hasIP() && pppHasIp.load();
        selectUplink(policy.update(now, wifiReady(), lteReady));

        if (lteReady) {
            if (!hadIp) {
                String ip = PPP.localIP().toString();
                portENTER_CRITICAL(&statusMux);
                snprintf(modemStatus.ip, sizeof(modemStatus.ip), "%s", ip.c_str());
                portEXIT_CRITICAL(&statusMux);
                setModemState(ModemState::ONLINE, "none");
                systemLog("[LTE] PPP connected, IP: %s", ip.c_str());
            }
            hadIp = true;
            // Keep an established PPP session on standby while Wi-Fi is used.
            continue;
        }

        bool failed = false;
        const char* failureReason = nullptr;
        if (dataMode) {
            failed = hadIp || uint32_t(now - stageStarted) >= 60000;
            if (failed) failureReason = hadIp ? "PPP connection lost" : "PPP IP negotiation timed out";
            if (failed) systemLog("[LTE] PPP disconnected or IP negotiation timed out");
        }

        if (!failed && !dataMode && policy.shouldStartLte(now)) {
            if (!initialized && (!attempted || uint32_t(now - lastAttempt) >= 30000)) {
                attempted = true;
                lastAttempt = now;
                setModemState(ModemState::INITIALIZING);
                portENTER_CRITICAL(&statusMux);
                ++modemStatus.attempts;
                portEXIT_CRITICAL(&statusMux);
                systemLog("[LTE] No WiFi; starting modem on UART1 (115200 baud)");
                bool configured = PPP.setApn(apn) && PPP.setPin(nullptr) &&
                                  PPP.setPins(txPin, rxPin);
                // The A7670 uses standard PPP AT commands. DATA mode avoids
                // the A76xx-specific CMUX limitations documented by Espressif.
                initialized = configured && PPP.begin(PPP_MODEM_GENERIC, 1, 115200);
                stageStarted = millis();
                now = stageStarted;
                lastAttachPoll = stageStarted - 5000;
                failed = !initialized;
                if (failed) failureReason = configured ? "PPP.begin failed (AT/SIM/driver initialization; exact cause unavailable)" :
                                                        "PPP APN/PIN/UART configuration failed";
                else setModemState(ModemState::WAIT_REGISTRATION);
                if (failed) systemLog("[LTE] Modem initialization failed; check power, UART and SIM PIN (must be disabled)");
            }

            if (initialized && uint32_t(now - lastAttachPoll) >= 5000) {
                lastAttachPoll = now;
                bool attached = PPP.attached();
                int signal = PPP.RSSI();
                uint32_t sampledAt = millis();
                portENTER_CRITICAL(&statusMux);
                modemStatus.registered = attached;
                modemStatus.signal = signal;
                modemStatus.signalSampleAt = sampledAt;
                portEXIT_CRITICAL(&statusMux);
                if (attached) {
                    systemLog("[LTE] Registered; starting PPP data connection");
                    setModemState(ModemState::CONNECTING);
                    pppHasIp.store(false);
                    dataMode = PPP.mode(ESP_MODEM_MODE_DATA);
                    stageStarted = millis();
                    failed = !dataMode;
                    if (failed) failureReason = "Could not enter PPP data mode";
                    if (failed) systemLog("[LTE] Data mode failed; check APN and SIM data service");
                } else if (uint32_t(now - stageStarted) >= 120000) {
                    systemLog("[LTE] Network registration timed out; retrying later");
                    failed = true;
                    failureReason = "Mobile registration timed out";
                }
            }
        }

        if (failed) {
            setModemState(ModemState::RECOVERING, failureReason);
            pppHasIp.store(false);
            // Never leave MQTT pointing at a netif that is being destroyed.
            selectUplink(wifiReady() ? Uplink::WIFI : Uplink::OFFLINE);
            if (PPP.handle()) PPP.mode(ESP_MODEM_MODE_COMMAND);
            PPP.end();
            initialized = false;
            dataMode = false;
            hadIp = false;
            lastAttempt = millis();
            attempted = true;
            portENTER_CRITICAL(&statusMux);
            modemStatus.registered = false;
            modemStatus.signal = -1;
            modemStatus.ip[0] = '\0';
            portEXIT_CRITICAL(&statusMux);
            setModemState(ModemState::RETRY_WAIT);
        }
    }
}
} // namespace

void setupConnectivity() {
    modemStatus.enabledAtBoot = getLteEnabled();
    txPin = getLteTx();
    rxPin = getLteRx();
    snprintf(apn, sizeof(apn), "%s", getLteApn());
    if (!getLteEnabled()) {
        systemLog("[LTE] Disabled");
        return;
    }
    if (!getLteApn()[0] || txPin < 0 || rxPin < 0 || txPin == rxPin ||
        !isLteGpioValid(txPin) || !isLteGpioValid(rxPin)) {
        systemLog("[LTE] Disabled: set lte_apn and distinct, free lte_tx/lte_rx GPIOs, then reboot");
        setModemState(ModemState::CONFIG_ERROR, "Missing APN or invalid/distinct UART GPIOs required");
        return;
    }
    wifiConfigured = getWifiSSID()[0] != '\0';
    Network.begin();
    Network.onEvent(onModemEvent);
    if (wifiReady()) rememberDns(0);
    taskRunning.store(true);
    setModemState(ModemState::WAIT_WIFI);
    if (xTaskCreate(modemTask, "lte", 6144, nullptr, 1, nullptr) != pdPASS) {
        taskRunning.store(false);
        setModemState(ModemState::CONFIG_ERROR, "Could not create modem task");
        systemLog("[LTE] Could not create modem task; WiFi remains available");
    }
}

Uplink getActiveUplink() {
    if (!taskRunning.load()) return wifiReady() ? Uplink::WIFI : Uplink::OFFLINE;
    Uplink selected = activeUplink.load();
    if (selected == Uplink::WIFI && !wifiReady()) return Uplink::OFFLINE;
    if (selected == Uplink::LTE && !pppHasIp.load()) return Uplink::OFFLINE;
    return selected;
}

void printLteStatus() {
    ModemStatus snapshot;
    portENTER_CRITICAL(&statusMux);
    snapshot = modemStatus;
    portEXIT_CRITICAL(&statusMux);
    Uplink uplink = getActiveUplink();
    bool hasIp = pppHasIp.load();
    Serial.println("LTE status (cached; no AT commands sent):");
    Serial.printf("  Enabled at boot: %s\n", snapshot.enabledAtBoot ? "yes" : "no");
    Serial.printf("  Modem task: %s\n", taskRunning.load() ? "running" : "not running");
    Serial.printf("  State: %s (%lu s)\n", modemStateName(snapshot.state),
                  static_cast<unsigned long>(uint32_t(millis() - snapshot.stateSince) / 1000));
    Serial.printf("  Active uplink: %s\n", uplink == Uplink::WIFI ? "WiFi" : uplink == Uplink::LTE ? "4G" : "offline");
    Serial.printf("  APN at boot: %s\n", apn[0] ? apn : "not set");
    Serial.printf("  UART1: TX GPIO%d, RX GPIO%d, 115200 baud\n", txPin, rxPin);
    Serial.printf("  Attempts: %lu\n", static_cast<unsigned long>(snapshot.attempts));
    Serial.printf("  Last mobile attachment: %s\n", snapshot.registered ? "attached" : "not confirmed");
    if (snapshot.signal >= 0 && snapshot.signal <= 31) {
        Serial.printf("  Last signal (CSQ): %d (sample age %lu s; not polled in PPP data mode)\n",
                      snapshot.signal, static_cast<unsigned long>(uint32_t(millis() - snapshot.signalSampleAt) / 1000));
    } else {
        Serial.println("  Last signal (CSQ): unknown");
    }
    Serial.printf("  PPP has IP: %s\n", hasIp ? "yes" : "no");
    Serial.printf("  Last PPP IP: %s\n", snapshot.ip[0] ? snapshot.ip : "not available");
    Serial.printf("  Last error: %s\n", snapshot.lastError);
    if (getLteEnabled() != snapshot.enabledAtBoot || strcmp(getLteApn(), apn) != 0 ||
        getLteTx() != txPin || getLteRx() != rxPin) {
        Serial.println("  Stored LTE settings differ from runtime; reboot to apply.");
    }
}

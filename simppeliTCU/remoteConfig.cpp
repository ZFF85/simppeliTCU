#include "remoteConfig.h"
#include "remoteConfigProtocol.h"
#include "configuration.h"
#include "connectivity.h"
#include "systemLog.h"
#include "canLeafZE1.h"
#include <PubSubClient.h>
#include <cJSON.h>
#include <esp_random.h>

namespace {
char session[33];
uint32_t sessionCreated = 0;
RemoteSettings staged;
bool hasStaged = false;
bool applyPending = false;
uint32_t applyAt = 0;

RemoteSettings currentSettings() {
    RemoteSettings settings;
    snprintf(settings.ssid, sizeof(settings.ssid), "%s", getWifiSSID());
    snprintf(settings.password, sizeof(settings.password), "%s", getWifiPassword());
    snprintf(settings.fallbackSsid, sizeof(settings.fallbackSsid), "%s", getFallbackSSID());
    snprintf(settings.fallbackPassword, sizeof(settings.fallbackPassword), "%s", getFallbackPassword());
    settings.lteEnabled = getLteEnabled();
    snprintf(settings.apn, sizeof(settings.apn), "%s", getLteApn());
    settings.tx = getLteTx();
    settings.rx = getLteRx();
    settings.syslogEnabled = getSyslogEnabled();
    snprintf(settings.syslogHost, sizeof(settings.syslogHost), "%s", getSyslogHost());
    settings.syslogPort = getSyslogPort();
    return settings;
}

void newSession() {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); ++i) snprintf(session + i * 2, 3, "%02x", bytes[i]);
    sessionCreated = millis();
}

cJSON* publicSettings(const RemoteSettings& settings) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "ssid", settings.ssid);
    cJSON_AddBoolToObject(object, "password_set", settings.password[0] != '\0');
    cJSON_AddStringToObject(object, "fallback_ssid", settings.fallbackSsid);
    cJSON_AddBoolToObject(object, "fallback_password_set", settings.fallbackPassword[0] != '\0');
    cJSON_AddBoolToObject(object, "lte_enabled", settings.lteEnabled);
    cJSON_AddStringToObject(object, "lte_apn", settings.apn);
    cJSON_AddNumberToObject(object, "lte_tx", settings.tx);
    cJSON_AddNumberToObject(object, "lte_rx", settings.rx);
    cJSON_AddBoolToObject(object, "syslog_enabled", settings.syslogEnabled);
    cJSON_AddStringToObject(object, "syslog_host", settings.syslogHost);
    cJSON_AddNumberToObject(object, "syslog_port", settings.syslogPort);
    return object;
}

void reply(PubSubClient& client, const char* prefix, bool ok, const char* message, bool includeSettings = false) {
    char topic[160];
    snprintf(topic, sizeof(topic), "%sconfig/response", prefix);
    cJSON* response = cJSON_CreateObject();
    if (!response) return;
    cJSON_AddBoolToObject(response, "ok", ok);
    cJSON_AddStringToObject(response, "message", message);
    cJSON_AddBoolToObject(response, "pending", hasStaged);
    if (includeSettings) {
        cJSON_AddStringToObject(response, "session", session);
        uint32_t elapsed = millis() - sessionCreated;
        cJSON_AddNumberToObject(response, "session_expires_in_ms", elapsed < 600000 ? 600000 - elapsed : 0);
        RemoteSettings settings = hasStaged ? staged : currentSettings();
        cJSON_AddItemToObject(response, "settings", publicSettings(settings));
        Uplink uplink = getActiveUplink();
        cJSON_AddStringToObject(response, "uplink", uplink == Uplink::WIFI ? "wifi" : uplink == Uplink::LTE ? "4g" : "offline");
        cJSON_AddBoolToObject(response, "mqtt_connected", client.connected());
    }
    char buffer[1536];
    if (cJSON_PrintPreallocated(response, buffer, sizeof(buffer), false)) client.publish(topic, buffer, false);
    cJSON_Delete(response);
}
} // namespace

void remoteConfigConnected(PubSubClient& client, const char* prefix) {
    // PubSubClient does not expose the retained flag. Rotate the nonce on every
    // connection so retained/replayed SET/APPLY requests cannot reuse old sessions.
    newSession();
    char topic[160];
    snprintf(topic, sizeof(topic), "%sconfig/request", prefix);
    client.subscribe(topic);
}

bool handleRemoteConfig(PubSubClient& client, const char* prefix, const char* topic,
                        const uint8_t* payload, unsigned length) {
    char requestTopic[160];
    snprintf(requestTopic, sizeof(requestTopic), "%sconfig/request", prefix);
    if (strcmp(topic, requestTopic)) return false;
    if (applyPending) { reply(client, prefix, false, "apply already scheduled"); return true; }
    RemoteRequest request;
    RemoteSettings base = hasStaged ? staged : currentSettings();
    const char* error = decodeRemoteRequest(reinterpret_cast<const char*>(payload), length, base, request);
    if (error) { reply(client, prefix, false, error); return true; }
    if (request.action == RemoteAction::GET) {
        if (!remoteSessionValid(session, session, millis(), sessionCreated)) newSession();
        reply(client, prefix, true, hasStaged ? "staged settings (not yet stored/applied)" : "stored settings", true);
        return true;
    }
    if (!remoteSessionValid(request.session, session, millis(), sessionCreated)) {
        reply(client, prefix, false, "invalid/expired session; send get first");
        return true;
    }
    if (request.action == RemoteAction::SET) {
        staged = request.settings;
        hasStaged = true;
        reply(client, prefix, true, "staged in RAM; send apply to save and reboot");
        systemLog("[Config] Remote network settings staged (values redacted)");
        return true;
    }
    if (!hasStaged) error = "no staged settings";
    else if (!configurationStorageReady()) error = "NVS storage unavailable; cannot save remote settings";
    else error = validateRemoteApply(staged);
    if (error) { reply(client, prefix, false, error); return true; }
    if (activeSequence != CanSequence::NONE) {
        reply(client, prefix, false, "CAN sequence busy; retry apply when idle");
        return true;
    }
    applyPending = true;
    applyAt = millis();
    reply(client, prefix, true, "apply accepted; settings will be saved and device rebooted when CAN is idle");
    // Invalidate immediately; duplicate requests cannot schedule a second reboot.
    session[0] = '\0';
    return true;
}

void manageRemoteConfig() {
    if (!applyPending || uint32_t(millis() - applyAt) < 1000 || activeSequence != CanSequence::NONE) return;
    applyPending = false;
    // All validation precedes writes. Settings use the same setters/NVS as USB
    // CLI. Multi-key NVS writes are not a power-loss-atomic transaction.
    bool ok = setWifiSSID(staged.ssid) == ConfigStatus::OK &&
              setWifiPassword(staged.password) == ConfigStatus::OK &&
              setFallbackSSID(staged.fallbackSsid) == ConfigStatus::OK &&
              setFallbackPassword(staged.fallbackPassword) == ConfigStatus::OK &&
              setLteEnabled(staged.lteEnabled) == ConfigStatus::OK &&
              setLteApn(staged.apn) == ConfigStatus::OK &&
              setLteTx(staged.tx) == ConfigStatus::OK && setLteRx(staged.rx) == ConfigStatus::OK &&
              setSyslogEnabled(staged.syslogEnabled) == ConfigStatus::OK &&
              setSyslogHost(staged.syslogHost) == ConfigStatus::OK &&
              setSyslogPort(staged.syslogPort) == ConfigStatus::OK;
    systemLog(ok ? "[Config] Remote settings saved; rebooting" : "[Config] Settings write failed; rebooting to load stored configuration");
    delay(100);
    ESP.restart();
}

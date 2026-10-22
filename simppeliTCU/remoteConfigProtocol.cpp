#include "remoteConfigProtocol.h"
#include "networkValidation.h"
#include <cJSON.h>
#include <cmath>
#include <cstring>

namespace {
bool safeJsonDepth(const char* data, size_t size) {
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (size_t i = 0; i < size; ++i) {
        char c = data[i];
        if (!c) return false;
        // cJSON stores C strings: reject encoded NUL rather than truncating values.
        if (c == '\\' && i + 5 < size && memcmp(data + i, "\\u0000", 6) == 0) return false;
        if (escaped) { escaped = false; continue; }
        if (quoted && c == '\\') { escaped = true; continue; }
        if (c == '"') quoted = !quoted;
        if (!quoted && (c == '{' || c == '[') && ++depth > 4) return false;
        if (!quoted && (c == '}' || c == ']')) --depth;
    }
    return true;
}

bool uniqueKeys(const cJSON* object) {
    for (const cJSON* a = object->child; a; a = a->next) {
        if (!a->string) return false;
        for (const cJSON* b = a->next; b; b = b->next) {
            if (b->string && strcmp(a->string, b->string) == 0) return false;
        }
    }
    return true;
}

bool copyValue(const cJSON* value, char* output, size_t maximum) {
    if (!cJSON_IsString(value) || strlen(value->valuestring) > maximum) return false;
    strcpy(output, value->valuestring);
    return true;
}

bool integerValue(const cJSON* value, int minimum, int maximum, int& output) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < minimum || value->valuedouble > maximum ||
        floor(value->valuedouble) != value->valuedouble) return false;
    output = static_cast<int>(value->valuedouble);
    return true;
}

const char* parseSettings(const cJSON* settings, RemoteSettings& output) {
    if (!cJSON_IsObject(settings) || !settings->child || !uniqueKeys(settings)) return "settings must be a nonempty object with unique keys";
    for (const cJSON* field = settings->child; field; field = field->next) {
        const char* key = field->string;
        if (strcmp(key, "ssid") == 0) {
            if (!copyValue(field, output.ssid, 32)) return "ssid must be a string of at most 32 bytes";
        } else if (strcmp(key, "password") == 0) {
            if (!copyValue(field, output.password, 63)) return "invalid password type/length";
        } else if (strcmp(key, "fallback_ssid") == 0) {
            if (!copyValue(field, output.fallbackSsid, 32)) return "fallback_ssid must be a string of at most 32 bytes";
        } else if (strcmp(key, "fallback_password") == 0) {
            if (!copyValue(field, output.fallbackPassword, 63)) return "invalid fallback_password type/length";
        } else if (strcmp(key, "lte_apn") == 0) {
            if (!copyValue(field, output.apn, 63) || !validNetworkName(output.apn, true)) return "invalid lte_apn";
        } else if (strcmp(key, "lte_tx") == 0 || strcmp(key, "lte_rx") == 0) {
            int& pin = strcmp(key, "lte_tx") == 0 ? output.tx : output.rx;
            if (!integerValue(field, -1, 48, pin) || !validModemGpio(pin)) return "invalid or reserved LTE GPIO";
        } else if (strcmp(key, "lte_enabled") == 0) {
            if (!cJSON_IsBool(field)) return "lte_enabled must be a JSON boolean";
            output.lteEnabled = cJSON_IsTrue(field);
        } else if (strcmp(key, "syslog_enabled") == 0) {
            if (!cJSON_IsBool(field)) return "syslog_enabled must be a JSON boolean";
            output.syslogEnabled = cJSON_IsTrue(field);
        } else if (strcmp(key, "syslog_host") == 0) {
            if (!copyValue(field, output.syslogHost, 63) || !validNetworkName(output.syslogHost)) return "invalid syslog_host";
        } else if (strcmp(key, "syslog_port") == 0) {
            if (!integerValue(field, 1, 65535, output.syslogPort)) return "invalid syslog_port";
        } else return "unsupported setting";
    }
    return nullptr;
}
} // namespace

const char* decodeRemoteRequest(const char* payload, size_t length,
                               const RemoteSettings& base, RemoteRequest& request) {
    if (!payload || !length || length > 1024 || !safeJsonDepth(payload, length)) return "invalid JSON size/depth/string";
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(payload, length, &end, false);
    if (!root) return "invalid JSON";
    auto cleanup = [&]() { cJSON_Delete(root); };
    while (end < payload + length && isspace(static_cast<unsigned char>(*end))) ++end;
    if (end != payload + length || !cJSON_IsObject(root) || !uniqueKeys(root)) {
        cleanup(); return "expected one JSON object with unique keys";
    }
    for (const cJSON* f = root->child; f; f = f->next) {
        if (strcmp(f->string, "action") && strcmp(f->string, "session") && strcmp(f->string, "settings")) {
            cleanup(); return "unsupported request field";
        }
    }
    const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(action)) { cleanup(); return "action is required"; }
    RemoteRequest candidate;
    candidate.settings = base;
    if (strcmp(action->valuestring, "get") == 0) candidate.action = RemoteAction::GET;
    else if (strcmp(action->valuestring, "set") == 0) candidate.action = RemoteAction::SET;
    else if (strcmp(action->valuestring, "apply") == 0) candidate.action = RemoteAction::APPLY;
    else { cleanup(); return "unknown action"; }
    const cJSON* session = cJSON_GetObjectItemCaseSensitive(root, "session");
    if (candidate.action != RemoteAction::GET && !copyValue(session, candidate.session, 32)) {
        cleanup(); return "session string is required";
    }
    const cJSON* settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
    const char* error = nullptr;
    if (candidate.action == RemoteAction::SET) error = parseSettings(settings, candidate.settings);
    else if (settings) error = "settings are only allowed for set";
    if (!error) request = candidate;
    cleanup();
    return error;
}

const char* validateRemoteApply(const RemoteSettings& settings) {
    size_t primaryPasswordLength = strlen(settings.password);
    size_t fallbackPasswordLength = strlen(settings.fallbackPassword);
    if ((settings.ssid[0] && primaryPasswordLength > 0 && primaryPasswordLength < 8) ||
        (settings.fallbackSsid[0] && fallbackPasswordLength > 0 && fallbackPasswordLength < 8)) {
        return "WiFi passwords must be empty (open network) or at least 8 bytes";
    }
    if (settings.fallbackSsid[0] && !settings.ssid[0]) return "fallback WiFi requires a primary SSID";
    if (settings.lteEnabled && (!settings.apn[0] || settings.tx < 0 || settings.rx < 0 ||
        settings.tx == settings.rx || !validModemGpio(settings.tx) || !validModemGpio(settings.rx))) {
        return "enabled LTE requires APN and distinct, valid TX/RX pins";
    }
    if (!settings.ssid[0] && !settings.lteEnabled) return "keep at least one WiFi/LTE uplink configured";
    if (settings.syslogEnabled && !settings.syslogHost[0]) return "enabled syslog requires a host";
    return nullptr;
}

bool remoteSessionValid(const char* supplied, const char* current, uint32_t now, uint32_t createdAt) {
    return current[0] && strlen(supplied) == 32 && strcmp(supplied, current) == 0 &&
           uint32_t(now - createdAt) < 600000;
}

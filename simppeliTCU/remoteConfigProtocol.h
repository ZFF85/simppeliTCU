#pragma once

#include <stddef.h>
#include <stdint.h>

struct RemoteSettings {
    char ssid[64] = {};
    char password[64] = {};
    char fallbackSsid[64] = {};
    char fallbackPassword[64] = {};
    bool lteEnabled = false;
    char apn[64] = {};
    int tx = -1;
    int rx = -1;
    bool syslogEnabled = false;
    char syslogHost[64] = {};
    int syslogPort = 514;
};

enum class RemoteAction { GET, SET, APPLY };
struct RemoteRequest {
    RemoteAction action = RemoteAction::GET;
    char session[33] = {};
    RemoteSettings settings;
};

// Pure parsing/validation; never changes stored settings or echoes secrets.
const char* decodeRemoteRequest(const char* payload, size_t length,
                               const RemoteSettings& base, RemoteRequest& request);
const char* validateRemoteApply(const RemoteSettings& settings);
bool remoteSessionValid(const char* supplied, const char* current,
                        uint32_t now, uint32_t createdAt);

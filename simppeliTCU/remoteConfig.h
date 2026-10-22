#pragma once

#include <stdint.h>
class PubSubClient;

void remoteConfigConnected(PubSubClient& client, const char* prefix);
bool handleRemoteConfig(PubSubClient& client, const char* prefix, const char* topic,
                        const uint8_t* payload, unsigned length);
void manageRemoteConfig();

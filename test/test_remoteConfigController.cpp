#include "remoteConfig.h"
#include "remoteConfigProtocol.h"
#include "configuration.h"
#include "connectivity.h"
#include "canLeafZE1.h"
#include <PubSubClient.h>
#include <cJSON.h>
#include <gtest/gtest.h>
#include <string>

namespace {
RemoteSettings stored;
unsigned long now = 0;
unsigned writes = 0;
unsigned restarts = 0;
bool storageReady = true;
const char* prefix = "ovms/test/car/";

std::string request(PubSubClient& client, const std::string& payload) {
    EXPECT_TRUE(handleRemoteConfig(client, prefix, "ovms/test/car/config/request",
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    EXPECT_FALSE(client.messages.empty());
    EXPECT_FALSE(client.messages.back().retained);
    return client.messages.back().payload;
}
std::string getSession(PubSubClient& client) {
    std::string response = request(client, R"({"action":"get"})");
    cJSON* root = cJSON_Parse(response.c_str());
    EXPECT_NE(root, nullptr);
    cJSON* token = cJSON_GetObjectItemCaseSensitive(root, "session");
    std::string result = cJSON_IsString(token) ? token->valuestring : "";
    cJSON_Delete(root);
    return result;
}
} // namespace

ESPClass ESP;
CanSequence activeSequence = CanSequence::NONE;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
void ESPClass::restart() { ++restarts; }
void esp_fill_random(void* output, size_t length) {
    static uint8_t value = 0;
    auto* data = static_cast<uint8_t*>(output);
    while (length--) *data++ = ++value;
}
void systemLog(const char*, ...) {}
Uplink getActiveUplink() { return Uplink::WIFI; }
const char* getWifiSSID() { return stored.ssid; }
const char* getWifiPassword() { return stored.password; }
const char* getFallbackSSID() { return stored.fallbackSsid; }
const char* getFallbackPassword() { return stored.fallbackPassword; }
bool getLteEnabled() { return stored.lteEnabled; }
const char* getLteApn() { return stored.apn; }
int getLteTx() { return stored.tx; }
int getLteRx() { return stored.rx; }
bool getSyslogEnabled() { return stored.syslogEnabled; }
const char* getSyslogHost() { return stored.syslogHost; }
int getSyslogPort() { return stored.syslogPort; }
bool configurationStorageReady() { return storageReady; }
#define MOCK_STRING_SETTER(name, member) \
ConfigStatus name(const char* value) { ++writes; strcpy(stored.member, value); return ConfigStatus::OK; }
#define MOCK_VALUE_SETTER(name, type, member) \
ConfigStatus name(type value) { ++writes; stored.member = value; return ConfigStatus::OK; }
MOCK_STRING_SETTER(setWifiSSID, ssid)
MOCK_STRING_SETTER(setWifiPassword, password)
MOCK_STRING_SETTER(setFallbackSSID, fallbackSsid)
MOCK_STRING_SETTER(setFallbackPassword, fallbackPassword)
MOCK_STRING_SETTER(setLteApn, apn)
MOCK_STRING_SETTER(setSyslogHost, syslogHost)
MOCK_VALUE_SETTER(setLteEnabled, bool, lteEnabled)
MOCK_VALUE_SETTER(setLteTx, int, tx)
MOCK_VALUE_SETTER(setLteRx, int, rx)
MOCK_VALUE_SETTER(setSyslogEnabled, bool, syslogEnabled)
MOCK_VALUE_SETTER(setSyslogPort, int, syslogPort)

TEST(RemoteController, StagingRedactionReplayAndIdleApply) {
    strcpy(stored.ssid, "Home");
    strcpy(stored.password, "OriginalSecret");
    PubSubClient client;
    remoteConfigConnected(client, prefix);
    ASSERT_EQ(client.subscriptions.front(), "ovms/test/car/config/request");
    std::string session = getSession(client);
    ASSERT_EQ(session.size(), 32u);
    EXPECT_EQ(client.messages.back().payload.find("OriginalSecret"), std::string::npos);

    std::string response = request(client, "{\"action\":\"set\",\"session\":\"" + session +
        "\",\"settings\":{\"ssid\":\"NewHome\",\"password\":\"ChangedSecret\"}}");
    EXPECT_NE(response.find("\"ok\":true"), std::string::npos);
    EXPECT_EQ(writes, 0u);
    EXPECT_STREQ(stored.ssid, "Home");
    EXPECT_EQ(request(client, R"({"action":"get"})").find("ChangedSecret"), std::string::npos);

    remoteConfigConnected(client, prefix); // Same-boot reconnect rotates session.
    response = request(client, "{\"action\":\"apply\",\"session\":\"" + session + "\"}");
    EXPECT_NE(response.find("\"ok\":false"), std::string::npos);
    EXPECT_EQ(writes, 0u);
    session = getSession(client);
    storageReady = false;
    response = request(client, "{\"action\":\"apply\",\"session\":\"" + session + "\"}");
    EXPECT_NE(response.find("NVS storage unavailable"), std::string::npos);
    EXPECT_EQ(writes, 0u);
    storageReady = true;
    activeSequence = CanSequence::CHARGE_ON;
    response = request(client, "{\"action\":\"apply\",\"session\":\"" + session + "\"}");
    EXPECT_NE(response.find("CAN sequence busy"), std::string::npos);
    activeSequence = CanSequence::NONE;
    response = request(client, "{\"action\":\"apply\",\"session\":\"" + session + "\"}");
    EXPECT_NE(response.find("\"ok\":true"), std::string::npos);
    manageRemoteConfig();
    EXPECT_EQ(writes, 0u); // Give the response a chance to reach the broker.
    now += 1001;
    activeSequence = CanSequence::CHARGE_ON;
    manageRemoteConfig();
    EXPECT_EQ(writes, 0u);
    activeSequence = CanSequence::NONE;
    manageRemoteConfig();
    EXPECT_GT(writes, 0u);
    EXPECT_EQ(restarts, 1u);
    EXPECT_STREQ(stored.ssid, "NewHome");
    EXPECT_STREQ(stored.password, "ChangedSecret");
    manageRemoteConfig();
    EXPECT_EQ(restarts, 1u);
}

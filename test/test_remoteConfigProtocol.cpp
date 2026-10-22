#include "remoteConfigProtocol.h"
#include "syslogFormat.h"
#include <gtest/gtest.h>
#include <cstring>
#include <limits>
#include <string>

namespace {
const char* decode(const char* json, const RemoteSettings& base, RemoteRequest& output) {
    return decodeRemoteRequest(json, strlen(json), base, output);
}
RemoteSettings workingSettings() {
    RemoteSettings settings;
    strcpy(settings.ssid, "Home");
    strcpy(settings.password, "OldSecret");
    return settings;
}
} // namespace

TEST(RemoteConfig, GetAndCaseSensitivePasswordPatch) {
    RemoteSettings base = workingSettings();
    RemoteRequest request;
    ASSERT_EQ(decode("{\"action\":\"get\"} \n", base, request), nullptr);
    EXPECT_EQ(request.action, RemoteAction::GET);
    ASSERT_EQ(decode(R"({"action":"set","session":"12345678901234567890123456789012","settings":{"password":"CaSe Sensitive!","syslog_enabled":true,"syslog_host":"logs.example.com","syslog_port":5514}})", base, request), nullptr);
    EXPECT_STREQ(request.settings.password, "CaSe Sensitive!");
    EXPECT_STREQ(request.settings.ssid, "Home");
    EXPECT_EQ(request.settings.syslogPort, 5514);
    EXPECT_TRUE(request.settings.syslogEnabled);
    EXPECT_STREQ(base.password, "OldSecret");
    EXPECT_EQ(validateRemoteApply(request.settings), nullptr);
}

TEST(RemoteConfig, InvalidPatchDoesNotPartiallyChangeOutput) {
    RemoteRequest request;
    strcpy(request.settings.ssid, "Sentinel");
    EXPECT_NE(decode(R"({"action":"set","session":"token","settings":{"ssid":"New","mqtt_password":"not-allowed"}})", workingSettings(), request), nullptr);
    EXPECT_STREQ(request.settings.ssid, "Sentinel");
}

TEST(RemoteConfig, RejectsMalformedDuplicateAndNestedRequests) {
    const char* invalid[] = {
        "[]", "null", "{}", "{\"action\":\"get\"} garbage",
        R"({"action":"get","action":"apply"})",
        R"({"action":"get","settings":{}})",
        R"({"action":"set","settings":{"ssid":"A"}})",
        R"({"action":"set","session":"t","settings":{"ssid":"A","ssid":"B"}})",
        R"({"action":"set","session":"t","settings":{"password":"a\u0000b"}})",
        R"({"action":"set","session":"t","settings":{"ssid":[[[[[[]]]]]]}})",
        R"({"action":"set","session":"t","settings":{}})",
        R"({"action":"set","session":"t","settings":{"syslog_host":"host\nforged-log"}})"
    };
    for (auto json : invalid) {
        RemoteRequest request;
        EXPECT_NE(decode(json, workingSettings(), request), nullptr) << json;
    }
    RemoteRequest request;
    std::string tooLong(1025, ' ');
    EXPECT_NE(decodeRemoteRequest(tooLong.data(), tooLong.size(), workingSettings(), request), nullptr);
}

TEST(RemoteConfig, StrictTypesAndGpioProtection) {
    const char* invalid[] = {
        R"({"lte_enabled":"true"})", R"({"lte_enabled":1})",
        R"({"lte_tx":15})", R"({"lte_tx":16})", R"({"lte_rx":19})",
        R"({"lte_rx":22})", R"({"lte_tx":26})", R"({"lte_tx":43})",
        R"({"lte_rx":45})", R"({"lte_tx":1.5})", R"({"lte_tx":"1"})",
        R"({"syslog_port":0})", R"({"syslog_port":65536})", R"({"syslog_port":1e309})",
        R"({"syslog_host":"https://example.com"})", R"({"lte_apn":"internet\"bad"})"
    };
    for (auto settings : invalid) {
        std::string json = "{\"action\":\"set\",\"session\":\"t\",\"settings\":" + std::string(settings) + "}";
        RemoteRequest request;
        EXPECT_NE(decode(json.c_str(), workingSettings(), request), nullptr) << settings;
    }
    RemoteRequest request;
    ASSERT_EQ(decode(R"({"action":"set","session":"t","settings":{"lte_tx":1,"lte_rx":2,"lte_apn":"test.apn","lte_enabled":true}})", workingSettings(), request), nullptr);
    EXPECT_EQ(validateRemoteApply(request.settings), nullptr);
}

TEST(RemoteConfig, CrossFieldValidationPreventsObviousLockout) {
    RemoteSettings settings;
    EXPECT_NE(validateRemoteApply(settings), nullptr);
    strcpy(settings.fallbackSsid, "Phone");
    EXPECT_NE(validateRemoteApply(settings), nullptr);
    settings = workingSettings();
    settings.syslogEnabled = true;
    EXPECT_NE(validateRemoteApply(settings), nullptr);
    settings.syslogEnabled = false;
    settings.lteEnabled = true;
    strcpy(settings.apn, "test.apn");
    settings.tx = settings.rx = 1;
    EXPECT_NE(validateRemoteApply(settings), nullptr);
    settings.rx = 2;
    settings.ssid[0] = '\0';
    EXPECT_EQ(validateRemoteApply(settings), nullptr); // LTE-only is permitted.
}

TEST(RemoteConfig, SessionExpiryRotationAndMillisRollover) {
    const char* token = "12345678901234567890123456789012";
    EXPECT_TRUE(remoteSessionValid(token, token, 100, 0));
    EXPECT_FALSE(remoteSessionValid("old", token, 100, 0));
    EXPECT_FALSE(remoteSessionValid(token, "", 100, 0));
    EXPECT_FALSE(remoteSessionValid(token, token, 600000, 0));
    uint32_t start = std::numeric_limits<uint32_t>::max() - 100;
    EXPECT_TRUE(remoteSessionValid(token, token, start + uint32_t(599999), start));
    EXPECT_FALSE(remoteSessionValid(token, token, start + uint32_t(600000), start));
}

TEST(SyslogFormat, Rfc5424AndSingleLineSanitization) {
    char output[384];
    size_t length = formatSyslog(output, sizeof(output), "leaf", "[LTE] Connected\r\nnext");
    EXPECT_STREQ(output, "<134>1 - leaf simppeliTCU - - - [LTE] Connected  next");
    EXPECT_EQ(length, strlen(output));
    char small[8];
    EXPECT_EQ(formatSyslog(small, sizeof(small), "leaf", "message"), 7u);
    EXPECT_EQ(small[7], '\0');
    EXPECT_EQ(formatSyslog(small, 0, "leaf", "message"), 0u);
}

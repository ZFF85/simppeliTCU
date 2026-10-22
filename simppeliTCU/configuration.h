#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <Arduino.h>

enum class ConfigStatus {
    OK,
    ERROR_UNKNOWN,
    ERROR_LENGTH,
    ERROR_PASSWORD_LENGTH,
    ERROR_WEB_PASSWORD_LENGTH,
    ERROR_HOSTNAME_LENGTH,
    ERROR_HOSTNAME_HYPHEN,
    ERROR_HOSTNAME_CHARS,
    ERROR_PORT_RANGE,
    ERROR_LTE_GPIO,
    ERROR_LTE_APN,
    ERROR_SYSLOG_HOST,
    ERROR_STORAGE
};

void initConfiguration();
void factoryReset();
bool configurationStorageReady();

const char* getWifiSSID();
ConfigStatus setWifiSSID(const char* value);

const char* getWifiPassword();
ConfigStatus setWifiPassword(const char* value);

const char* getFallbackSSID();
ConfigStatus setFallbackSSID(const char* value);

const char* getFallbackPassword();
ConfigStatus setFallbackPassword(const char* value);

const char* getApSSID();
ConfigStatus setApSSID(const char* value);

const char* getApPassword();
ConfigStatus setApPassword(const char* value);

const char* getWebPassword();
ConfigStatus setWebPassword(const char* value);

bool getLteEnabled();
ConfigStatus setLteEnabled(bool value);
const char* getLteApn();
ConfigStatus setLteApn(const char* value);
int getLteTx();
ConfigStatus setLteTx(int value);
int getLteRx();
ConfigStatus setLteRx(int value);
bool isLteGpioValid(int value);

bool getSyslogEnabled();
ConfigStatus setSyslogEnabled(bool value);
const char* getSyslogHost();
ConfigStatus setSyslogHost(const char* value);
int getSyslogPort();
ConfigStatus setSyslogPort(int value);

const char* getHostName();
ConfigStatus setHostName(const char* value);

const char* getMqttServer();
ConfigStatus setMqttServer(const char* value);

int getMqttPort();
ConfigStatus setMqttPort(int value);

const char* getMqttUser();
ConfigStatus setMqttUser(const char* value);

const char* getMqttPassword();
ConfigStatus setMqttPassword(const char* value);

const char* getVehicleId();
ConfigStatus setVehicleId(const char* value);

bool getLockingEnabled();
ConfigStatus setLockingEnabled(bool value);

bool getMqttTls();
ConfigStatus setMqttTls(bool value);

bool getHassDiscoveryEnabled();
ConfigStatus setHassDiscoveryEnabled(bool value);

#endif

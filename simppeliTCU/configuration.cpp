#include "configuration.h"
#include <Preferences.h>
#include "stringBuffer.h"
#include "networkValidation.h"

static Preferences preferences;
static bool nvsReady = false;

template <size_t N>
class ConfigString {
private:
    StringBuffer<N> value;
    const char* key;
    const char* default_val;
public:
    ConfigString(const char* k, const char* def) : key(k), default_val(def) {}
    
    void load() {
        if (nvsReady && preferences.isKey(key)) {
            preferences.getString(key, value.data(), value.capacity());
        } else {
            value.copyFrom(default_val);
            if (nvsReady) preferences.putString(key, value.c_str());
        }
    }
    
    ConfigStatus set(const char* val) {
        if (strlen(val) >= value.capacity()) {
            return ConfigStatus::ERROR_LENGTH;
        }
        if (nvsReady) {
            size_t length = strlen(val);
            if (preferences.putString(key, val) != length ||
                (length == 0 && (!preferences.isKey(key) || preferences.getString(key, "!").length() != 0))) {
                return ConfigStatus::ERROR_STORAGE;
            }
        }
        value.copyFrom(val);
        return ConfigStatus::OK;
    }
    
    const char* get() const { 
        return value.c_str(); 
    }
};

class ConfigInt {
private:
    int value;
    const char* key;
    int default_val;
public:
    ConfigInt(const char* k, int def) : key(k), default_val(def) {}
    
    void load() {
        if (nvsReady && preferences.isKey(key)) {
            value = preferences.getInt(key, default_val);
        } else {
            value = default_val;
            if (nvsReady) preferences.putInt(key, value);
        }
    }
    
    ConfigStatus set(int val) {
        if (nvsReady && preferences.putInt(key, val) != sizeof(int32_t)) return ConfigStatus::ERROR_STORAGE;
        value = val;
        return ConfigStatus::OK;
    }
    
    int get() const { 
        return value; 
    }
};

class ConfigBool {
private:
    bool value;
    const char* key;
    bool default_val;
public:
    ConfigBool(const char* k, bool def) : value(def), key(k), default_val(def) {}
    
    void load() {
        if (nvsReady && preferences.isKey(key)) {
            value = preferences.getBool(key, default_val);
        } else {
            value = default_val;
            if (nvsReady) preferences.putBool(key, value);
        }
    }
    
    ConfigStatus set(bool val) {
        if (nvsReady && preferences.putBool(key, val) != sizeof(uint8_t)) return ConfigStatus::ERROR_STORAGE;
        value = val;
        return ConfigStatus::OK;
    }
    
    bool get() const { 
        return value; 
    }
};

// Define config objects
static ConfigString<64> conf_ssid("ssid", "");
static ConfigString<64> conf_password("password", "");
static ConfigString<64> conf_fallback_ssid("fallback_ssid", "");
static ConfigString<64> conf_fallback_password("fallback_password", "");
static ConfigString<64> conf_ap_ssid("ap_ssid", "");
static ConfigString<64> conf_ap_password("ap_password", "");
static ConfigString<64> conf_web_password("web_password", "");
static ConfigBool       conf_lte_enabled("lte_enabled", false);
static ConfigString<64> conf_lte_apn("lte_apn", "");
static ConfigInt        conf_lte_tx("lte_tx", -1);
static ConfigInt        conf_lte_rx("lte_rx", -1);
static ConfigBool       conf_syslog_enabled("syslog_enabled", false);
static ConfigString<64> conf_syslog_host("syslog_host", "");
static ConfigInt        conf_syslog_port("syslog_port", 514);
static ConfigString<64> conf_hostName("hostName", "leaf");
static ConfigString<64> conf_mqtt_server("mqtt_server", "");
static ConfigInt        conf_mqtt_port("mqtt_port", 8883);
static ConfigString<64> conf_mqtt_user("mqtt_user", "");
static ConfigString<64> conf_mqtt_password("mqtt_password", "");
static ConfigString<64> conf_vehicle_id("vehicle_id", "");
static ConfigBool       conf_locking_enabled("locking_enabled", false);
static ConfigBool       conf_mqtt_tls("mqtt_tls", true);
static ConfigBool       conf_hass_discovery("hass_discovery", true);

void initConfiguration() {
    nvsReady = preferences.begin("config", false);
    if (!nvsReady) {
        Serial.println("Error: Failed to initialize NVS (Preferences). Falling back to RAM defaults.");
    }

    conf_ssid.load();
    conf_password.load();
    conf_fallback_ssid.load();
    conf_fallback_password.load();
    conf_ap_ssid.load();
    conf_ap_password.load();
    conf_web_password.load();
    conf_lte_enabled.load();
    conf_lte_apn.load();
    conf_lte_tx.load();
    conf_lte_rx.load();
    conf_syslog_enabled.load();
    conf_syslog_host.load();
    conf_syslog_port.load();
    conf_hostName.load();
    conf_mqtt_server.load();
    conf_mqtt_port.load();
    conf_mqtt_user.load();
    conf_mqtt_password.load();
    conf_vehicle_id.load();
    conf_locking_enabled.load();
    conf_mqtt_tls.load();
    conf_hass_discovery.load();
}

void factoryReset() {
    if (!nvsReady) {
        Serial.println("Error: Factory reset failed because NVS is not initialized.");
        return;
    }

    if (preferences.clear()) {
        Serial.println("Factory reset completed successfully.");
    } else {
        Serial.println("Error: Factory reset failed while clearing NVS.");
    }
}

bool configurationStorageReady() { return nvsReady; }

const char* getWifiSSID() { return conf_ssid.get(); }
ConfigStatus setWifiSSID(const char* value) { return conf_ssid.set(value); }

const char* getWifiPassword() { return conf_password.get(); }
ConfigStatus setWifiPassword(const char* value) { return conf_password.set(value); }

const char* getFallbackSSID() { return conf_fallback_ssid.get(); }
ConfigStatus setFallbackSSID(const char* value) { return conf_fallback_ssid.set(value); }

const char* getFallbackPassword() { return conf_fallback_password.get(); }
ConfigStatus setFallbackPassword(const char* value) { return conf_fallback_password.set(value); }

const char* getApSSID() { return conf_ap_ssid.get(); }
ConfigStatus setApSSID(const char* value) { return conf_ap_ssid.set(value); }

const char* getApPassword() { return conf_ap_password.get(); }
ConfigStatus setApPassword(const char* value) { 
    size_t len = strlen(value);
    if (len > 0 && len < 8) {
        return ConfigStatus::ERROR_PASSWORD_LENGTH;
    }
    return conf_ap_password.set(value); 
}

const char* getWebPassword() { return conf_web_password.get(); }
ConfigStatus setWebPassword(const char* value) {
    size_t len = strlen(value);
    if (len > 0 && len < 8) {
        return ConfigStatus::ERROR_WEB_PASSWORD_LENGTH;
    }
    return conf_web_password.set(value);
}

bool getLteEnabled() { return conf_lte_enabled.get(); }
ConfigStatus setLteEnabled(bool value) { return conf_lte_enabled.set(value); }

const char* getLteApn() { return conf_lte_apn.get(); }
ConfigStatus setLteApn(const char* value) {
    for (const char* p = value; *p; ++p) {
        if (!isalnum(static_cast<unsigned char>(*p)) && *p != '.' && *p != '-' && *p != '_') {
            return ConfigStatus::ERROR_LTE_APN;
        }
    }
    return conf_lte_apn.set(value);
}

bool isLteGpioValid(int value) {
    return validModemGpio(value);
}

int getLteTx() { return conf_lte_tx.get(); }
ConfigStatus setLteTx(int value) {
    if (!isLteGpioValid(value)) return ConfigStatus::ERROR_LTE_GPIO;
    return conf_lte_tx.set(value);
}

int getLteRx() { return conf_lte_rx.get(); }
ConfigStatus setLteRx(int value) {
    if (!isLteGpioValid(value)) return ConfigStatus::ERROR_LTE_GPIO;
    return conf_lte_rx.set(value);
}

bool getSyslogEnabled() { return conf_syslog_enabled.get(); }
ConfigStatus setSyslogEnabled(bool value) { return conf_syslog_enabled.set(value); }
const char* getSyslogHost() { return conf_syslog_host.get(); }
ConfigStatus setSyslogHost(const char* value) {
    if (!validNetworkName(value)) return ConfigStatus::ERROR_SYSLOG_HOST;
    return conf_syslog_host.set(value);
}
int getSyslogPort() { return conf_syslog_port.get(); }
ConfigStatus setSyslogPort(int value) {
    if (value < 1 || value > 65535) return ConfigStatus::ERROR_PORT_RANGE;
    return conf_syslog_port.set(value);
}

const char* getHostName() { return conf_hostName.get(); }
ConfigStatus setHostName(const char* value) {
    size_t len = strlen(value);
    if (len < 1 || len > 15) {
        return ConfigStatus::ERROR_HOSTNAME_LENGTH;
    }
    if (value[0] == '-' || value[len - 1] == '-') {
        return ConfigStatus::ERROR_HOSTNAME_HYPHEN;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(value[i]) && value[i] != '-') {
            return ConfigStatus::ERROR_HOSTNAME_CHARS;
        }
    }
    return conf_hostName.set(value);
}

const char* getMqttServer() { return conf_mqtt_server.get(); }
ConfigStatus setMqttServer(const char* value) { return conf_mqtt_server.set(value); }

int getMqttPort() { return conf_mqtt_port.get(); }
ConfigStatus setMqttPort(int value) { return conf_mqtt_port.set(value); }

const char* getMqttUser() { return conf_mqtt_user.get(); }
ConfigStatus setMqttUser(const char* value) { return conf_mqtt_user.set(value); }

const char* getMqttPassword() { return conf_mqtt_password.get(); }
ConfigStatus setMqttPassword(const char* value) { return conf_mqtt_password.set(value); }

const char* getVehicleId() { return conf_vehicle_id.get(); }
ConfigStatus setVehicleId(const char* value) { return conf_vehicle_id.set(value); }

bool getLockingEnabled() { return conf_locking_enabled.get(); }
ConfigStatus setLockingEnabled(bool value) { return conf_locking_enabled.set(value); }

bool getMqttTls() { return conf_mqtt_tls.get(); }
ConfigStatus setMqttTls(bool value) { return conf_mqtt_tls.set(value); }

bool getHassDiscoveryEnabled() { return conf_hass_discovery.get(); }
ConfigStatus setHassDiscoveryEnabled(bool value) { return conf_hass_discovery.set(value); }

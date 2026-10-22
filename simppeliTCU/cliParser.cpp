#include "cliParser.h"
#include <Arduino.h>
#include "configuration.h"
#include "connectivity.h"
#include "mqttInterface.h"
#include "systemLog.h"
#include <errno.h>

static char inputBuffer[128];
static int inputIndex = 0;

void executeCommand(char* cmd) {
    char* command = strtok(cmd, " ");
    if (command == NULL) return;

    if (strcmp(command, "list") == 0) {
        char* option = strtok(NULL, " ");
        bool showSecrets = (option != NULL && strcmp(option, "--secrets") == 0);

        Serial.println("Current Configuration:");
        Serial.printf("  ssid: %s\n", getWifiSSID());
        Serial.printf("  password: %s\n", showSecrets ? getWifiPassword() : "****");
        Serial.printf("  fallback_ssid: %s\n", getFallbackSSID());
        Serial.printf("  fallback_password: %s\n", showSecrets ? getFallbackPassword() : "****");
        Serial.printf("  ap_ssid: %s\n", getApSSID());
        Serial.printf("  ap_password: %s\n", showSecrets ? getApPassword() : "****");
        Serial.printf("  web_password: %s\n", showSecrets ? getWebPassword() : "****");
        Serial.printf("  lte_enabled: %s\n", getLteEnabled() ? "true" : "false");
        Serial.printf("  lte_apn: %s\n", getLteApn());
        Serial.printf("  lte_tx: %d\n", getLteTx());
        Serial.printf("  lte_rx: %d\n", getLteRx());
        Serial.printf("  syslog_enabled: %s\n", getSyslogEnabled() ? "true" : "false");
        Serial.printf("  syslog_host: %s\n", getSyslogHost());
        Serial.printf("  syslog_port: %d\n", getSyslogPort());
        Serial.printf("  hostName: %s\n", getHostName());
        Serial.printf("  mqtt_server: %s\n", getMqttServer());
        Serial.printf("  mqtt_port: %d\n", getMqttPort());
        Serial.printf("  mqtt_user: %s\n", getMqttUser());
        Serial.printf("  mqtt_password: %s\n", showSecrets ? getMqttPassword() : "****");
        Serial.printf("  vehicle_id: %s\n", getVehicleId());
        Serial.printf("  locking_enabled: %s\n", getLockingEnabled() ? "true" : "false");
        Serial.printf("  mqtt_tls: %s\n", getMqttTls() ? "true" : "false");
        Serial.printf("  hass_discovery: %s\n", getHassDiscoveryEnabled() ? "true" : "false");
        if (!showSecrets) {
            Serial.println("  (use 'list --secrets' to display secret values)");
        }
    } 
    else if (strcmp(command, "get") == 0) {
        char* key = strtok(NULL, " ");
        if (key != NULL) {
            if (strcmp(key, "ssid") == 0) Serial.println(getWifiSSID());
            else if (strcmp(key, "password") == 0) Serial.println("****");
            else if (strcmp(key, "fallback_ssid") == 0) Serial.println(getFallbackSSID());
            else if (strcmp(key, "fallback_password") == 0) Serial.println("****");
            else if (strcmp(key, "ap_ssid") == 0) Serial.println(getApSSID());
            else if (strcmp(key, "ap_password") == 0) Serial.println("****");
            else if (strcmp(key, "web_password") == 0) Serial.println("****");
            else if (strcmp(key, "lte_enabled") == 0) Serial.println(getLteEnabled() ? "true" : "false");
            else if (strcmp(key, "lte_apn") == 0) Serial.println(getLteApn());
            else if (strcmp(key, "lte_tx") == 0) Serial.println(getLteTx());
            else if (strcmp(key, "lte_rx") == 0) Serial.println(getLteRx());
            else if (strcmp(key, "syslog_enabled") == 0) Serial.println(getSyslogEnabled() ? "true" : "false");
            else if (strcmp(key, "syslog_host") == 0) Serial.println(getSyslogHost());
            else if (strcmp(key, "syslog_port") == 0) Serial.println(getSyslogPort());
            else if (strcmp(key, "hostName") == 0) Serial.println(getHostName());
            else if (strcmp(key, "mqtt_server") == 0) Serial.println(getMqttServer());
            else if (strcmp(key, "mqtt_port") == 0) Serial.println(getMqttPort());
            else if (strcmp(key, "mqtt_user") == 0) Serial.println(getMqttUser());
            else if (strcmp(key, "mqtt_password") == 0) Serial.println("****");
            else if (strcmp(key, "vehicle_id") == 0) Serial.println(getVehicleId());
            else if (strcmp(key, "locking_enabled") == 0) Serial.println(getLockingEnabled() ? "true" : "false");
            else if (strcmp(key, "mqtt_tls") == 0) Serial.println(getMqttTls() ? "true" : "false");
            else if (strcmp(key, "hass_discovery") == 0) Serial.println(getHassDiscoveryEnabled() ? "true" : "false");
            else Serial.println("Unknown parameter.");
        } else {
            Serial.println("Usage: get <key>");
        }
    } 
    else if (strcmp(command, "set") == 0) {
        char* key = strtok(NULL, " ");
        if (key != NULL) {
            char* val = strtok(NULL, ""); // Get rest of the string
            if (val == NULL) val = (char*)""; // Allow empty values

            ConfigStatus status = ConfigStatus::ERROR_UNKNOWN;

            if (strcmp(key, "ssid") == 0) status = setWifiSSID(val);
            else if (strcmp(key, "password") == 0) status = setWifiPassword(val);
            else if (strcmp(key, "fallback_ssid") == 0) status = setFallbackSSID(val);
            else if (strcmp(key, "fallback_password") == 0) status = setFallbackPassword(val);
            else if (strcmp(key, "ap_ssid") == 0) status = setApSSID(val);
            else if (strcmp(key, "ap_password") == 0) status = setApPassword(val);
            else if (strcmp(key, "web_password") == 0) status = setWebPassword(val);
            else if (strcmp(key, "lte_apn") == 0) status = setLteApn(val);
            else if (strcmp(key, "syslog_host") == 0) status = setSyslogHost(val);
            else if (strcmp(key, "syslog_port") == 0) {
                char* end = nullptr;
                errno = 0;
                long port = strtol(val, &end, 10);
                status = (!errno && end != val && *end == '\0' && port >= 1 && port <= 65535) ?
                         setSyslogPort(port) : ConfigStatus::ERROR_PORT_RANGE;
            }
            else if (strcmp(key, "syslog_enabled") == 0) {
                if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) status = setSyslogEnabled(true);
                else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) status = setSyslogEnabled(false);
                else {
                    Serial.println("Error: Value must be 'true', 'false', '1', or '0'.");
                    return;
                }
            }
            else if (strcmp(key, "lte_enabled") == 0) {
                if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
                    status = setLteEnabled(true);
                } else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
                    status = setLteEnabled(false);
                } else {
                    Serial.println("Error: Value must be 'true', 'false', '1', or '0'.");
                    return;
                }
            }
            else if (strcmp(key, "lte_tx") == 0 || strcmp(key, "lte_rx") == 0) {
                char* end = nullptr;
                errno = 0;
                long pin = strtol(val, &end, 10);
                if (errno || end == val || *end != '\0' || pin < -1 || pin > 48) {
                    status = ConfigStatus::ERROR_LTE_GPIO;
                } else {
                    status = strcmp(key, "lte_tx") == 0 ? setLteTx(pin) : setLteRx(pin);
                }
            }
            else if (strcmp(key, "hostName") == 0) status = setHostName(val);
            else if (strcmp(key, "mqtt_server") == 0) status = setMqttServer(val);
            else if (strcmp(key, "mqtt_port") == 0) {
                int port = atoi(val);
                if (port > 0 && port <= 65535) {
                    status = setMqttPort(port);
                } else {
                    status = ConfigStatus::ERROR_PORT_RANGE;
                }
            }
            else if (strcmp(key, "mqtt_user") == 0) status = setMqttUser(val);
            else if (strcmp(key, "mqtt_password") == 0) status = setMqttPassword(val);
            else if (strcmp(key, "vehicle_id") == 0) status = setVehicleId(val);
            else if (strcmp(key, "locking_enabled") == 0) {
                if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
                    status = setLockingEnabled(true);
                } else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
                    status = setLockingEnabled(false);
                } else {
                    Serial.println("Error: Value must be 'true', 'false', '1', or '0'.");
                    return;
                }
            }
            else if (strcmp(key, "mqtt_tls") == 0) {
                if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
                    status = setMqttTls(true);
                } else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
                    status = setMqttTls(false);
                } else {
                    Serial.println("Error: Value must be 'true', 'false', '1', or '0'.");
                    return;
                }
            }
            else if (strcmp(key, "hass_discovery") == 0) {
                if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
                    status = setHassDiscoveryEnabled(true);
                } else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
                    status = setHassDiscoveryEnabled(false);
                } else {
                    Serial.println("Error: Value must be 'true', 'false', '1', or '0'.");
                    return;
                }
            }
            else {
                Serial.println("Unknown parameter.");
                return;
            }
            
            if (status == ConfigStatus::OK) {
                Serial.println("Value updated.");
            } else if (status == ConfigStatus::ERROR_LENGTH) {
                Serial.println("Error: Value exceeds maximum allowed length.");
            } else if (status == ConfigStatus::ERROR_PASSWORD_LENGTH) {
                Serial.println("Error: AP Password must be either empty or at least 8 characters long.");
            } else if (status == ConfigStatus::ERROR_WEB_PASSWORD_LENGTH) {
                Serial.println("Error: Web password must be either empty or at least 8 characters long.");
            } else if (status == ConfigStatus::ERROR_HOSTNAME_LENGTH) {
                Serial.println("Error: Hostname must be 1-15 characters long.");
            } else if (status == ConfigStatus::ERROR_HOSTNAME_HYPHEN) {
                Serial.println("Error: Hostname cannot start or end with a hyphen.");
            } else if (status == ConfigStatus::ERROR_HOSTNAME_CHARS) {
                Serial.println("Error: Hostname can only contain letters, numbers, and hyphens.");
            } else if (status == ConfigStatus::ERROR_PORT_RANGE) {
                Serial.println("Error: Invalid port number. Must be 1-65535. Value not updated.");
            } else if (status == ConfigStatus::ERROR_LTE_GPIO) {
                Serial.println("Error: LTE GPIO must be -1 (unset) or a free pin; CAN, USB, flash/PSRAM, UART0 and strapping pins are reserved.");
            } else if (status == ConfigStatus::ERROR_LTE_APN) {
                Serial.println("Error: APN may contain only letters, digits, dots, hyphens and underscores.");
            } else if (status == ConfigStatus::ERROR_SYSLOG_HOST) {
                Serial.println("Error: Syslog host must be an IPv4 address or DNS hostname without a URL/port.");
            } else if (status == ConfigStatus::ERROR_STORAGE) {
                Serial.println("Error: Configuration could not be saved to NVS.");
            } else {
                Serial.println("Error: Failed to update value.");
            }
        } else {
            Serial.println("Usage: set <key> <value>");
        }
    } 
    else if (strcmp(command, "lte-status") == 0) {
        printLteStatus();
    }
    else if (strcmp(command, "mqtt-status") == 0) {
        printMqttStatus();
    }
    else if (strcmp(command, "syslog-status") == 0) {
        printSyslogStatus();
    }
    else if (strcmp(command, "reboot") == 0) {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    } 
    else if (strcmp(command, "factory-reset") == 0) {
        Serial.println("Performing factory reset...");
        factoryReset();
        delay(100);
        ESP.restart();
    }
    else {
        Serial.println("Unknown command.");
    }
}

void processSerialInput() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            if (inputIndex > 0) {
                inputBuffer[inputIndex] = '\0';
                executeCommand(inputBuffer);
                inputIndex = 0;
            }
        } 
        else if (inputIndex < (sizeof(inputBuffer) - 1)) {
            inputBuffer[inputIndex++] = c;
        }
    }
}

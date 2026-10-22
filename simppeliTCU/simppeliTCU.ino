#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include "configuration.h"
#include "cliParser.h"
#include "canLeafZE1.h"
#include "ui.h"
#include "mqttInterface.h"
#include "connectivity.h"
#include "remoteConfig.h"
#include "systemLog.h"

WebServer server(80);


// State variables
bool wifiEnabled = false;
bool carIsAwake = false;
static bool usingFallback = false;
static bool wasStaConnected = false;
static unsigned long wifiAttemptStart = 0;
static unsigned long lastPrimaryScan = 0;
static unsigned long primarySeenSince = 0;
static bool primarySeen = false;
static bool primaryScanRunning = false;

// Values read from car
float currentSOC = -1.0;
float cabinTemp = -99.0;
bool isChargingNow = false;
ChargerState currentChargerState = ChargerState::IDLE;
bool isHvacOn = false;
int8_t lockState = -1;
float currentSetpoint = -1.0;
float currentFanSpeed = -1.0;
bool isHeatingEnabled = false;
bool isCoolingEnabled = false;
VentilationMode currentVentilationMode = VentilationMode::UNKNOWN;

void resetData() {
    currentSOC = -1.0;
    cabinTemp = -99.0;
    isChargingNow = false;
    currentChargerState = ChargerState::IDLE;
    isHvacOn = false;
    lockState = -1;
    currentSetpoint = -1.0;
    currentFanSpeed = -1.0;
    isHeatingEnabled = false;
    isCoolingEnabled = false;
    currentVentilationMode = VentilationMode::UNKNOWN;
}

void handleRoot() {
  bool sequenceActive = (activeSequence != CanSequence::NONE);
  sendMainPage(server, currentSOC, cabinTemp, isChargingNow, currentChargerState, isHvacOn, sequenceActive, getLockingEnabled(), lockState, currentSetpoint, currentFanSpeed, isHeatingEnabled, isCoolingEnabled, currentVentilationMode);
}

bool authorizeWebUI() {
  const char* password = getWebPassword();
  if (password[0] == '\0') {
    server.send(503, "text/plain", "Web UI disabled. Set web_password via USB serial CLI.");
    return false;
  }
  if (!server.authenticate("admin", password)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void handleCarAwake() {
  carIsAwake = true;
}

void handleChargerStatus(bool isCharging, ChargerState state) {
  isChargingNow = isCharging;
  currentChargerState = state;
  mqttUpdateCharging(isCharging, state);
}

void handleHVACStatus(bool isOn) {
  isHvacOn = isOn;
  
  mqttUpdateHVAC(isOn);
}

void handleCabinTemp(float temp) {
  cabinTemp = temp;
  mqttUpdateCabinTemp(temp);
}

void handleHVACSetpoint(float setpoint) {
  // The car reports 0 for setpoint during remote climate. Ignore it if we already have a valid setpoint.
  if ((isHvacOn || activeSequence == CanSequence::HVAC_ON) && setpoint == 0.0f && currentSetpoint > 0.0f) {
      return;
  }
  currentSetpoint = setpoint;
  mqttUpdateHVACSetpoint(setpoint);
}

void handleFanSpeed(float speed) {
  // The car reports 0 for fan speed during remote climate even though it is blowing.
  if ((isHvacOn || activeSequence == CanSequence::HVAC_ON) && speed == 0.0f && currentFanSpeed > 0.0f) {
      return;
  }
  currentFanSpeed = speed;
  mqttUpdateFanSpeed(speed);
}

void handleHeatingMode(bool heating, bool cooling) {
  isHeatingEnabled = heating;
  isCoolingEnabled = cooling;
  mqttUpdateHeatingMode(heating, cooling);
}

void handleVentilationMode(VentilationMode mode) {
  currentVentilationMode = mode;
  mqttUpdateVentilationMode(mode);
}

void handleDoorStatus(bool fl, bool fr, bool rl, bool rr, bool trunk) {
  mqttUpdateDoors(fl, fr, rl, rr, trunk);
}

void handleLockStatus(bool locked) {
  lockState = locked ? 1 : 0;
  mqttUpdateLock(locked);
}

void handleRawSOC(float soc) {
  currentSOC = soc;
  mqttUpdateSOC(soc);
}

void logSequenceStart() {
  Serial.println("### Starting Sequence ###");
  Serial.println("# type(<,>,!) time(s) identifier data...");
}


// Update data (Wake up)
void handleRefresh() {
  Serial.println("### Refresh! ###");
  logSequenceStart();
  resetData();
  startSequence(CanSequence::REFRESH, millis());
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMqttRefresh() {
  Serial.println("### Refresh! (MQTT) ###");
  logSequenceStart();
  resetData();
  startSequence(CanSequence::REFRESH, millis());
}

void handleHvacOn() {
  Serial.println("### HVAC ON! ###");
  float setpoint = 0.0f;
  if (server.hasArg("setpoint")) {
    setpoint = server.arg("setpoint").toFloat();
  }
  Serial.print("Setpoint: ");
  Serial.println(setpoint);
  logSequenceStart();
  setHVACTargetTemperature(setpoint);
  if (setpoint > 0.0f) {
      handleHVACSetpoint(setpoint); // Pre-fill the UI/MQTT
  }
  startSequence(CanSequence::HVAC_ON, millis());
  server.sendHeader("Location", "/"); 
  server.send(303);
}

void handleMqttHvacOn(float setpoint) {
  Serial.print("### HVAC ON! (MQTT) Setpoint: ");
  Serial.println(setpoint);
  logSequenceStart();
  setHVACTargetTemperature(setpoint);
  if (setpoint > 0.0f) {
      handleHVACSetpoint(setpoint); // Pre-fill the UI/MQTT with the requested setpoint
  }
  startSequence(CanSequence::HVAC_ON, millis());
}

void handleHvacOff() {
  Serial.println("### HVAC OFF! ###");
  logSequenceStart();
  startSequence(CanSequence::HVAC_OFF, millis());
  server.sendHeader("Location", "/"); 
  server.send(303);
}

void handleMqttHvacOff() {
  Serial.println("### HVAC OFF! (MQTT) ###");
  logSequenceStart();
  startSequence(CanSequence::HVAC_OFF, millis());
}

void handleMqttLock() {
  if (!getLockingEnabled()) {
    Serial.println("### Lock Doors: Feature disabled (MQTT) ###");
    mqttPublishStatus("Door locking is disabled. Enable it via CLI: set locking_enabled true");
    return;
  }
  Serial.println("### Lock Doors! (MQTT) ###");
  logSequenceStart();
  startSequence(CanSequence::LOCK_DOORS, millis());
}

void handleMqttUnlock() {
  if (!getLockingEnabled()) {
    Serial.println("### Unlock Doors: Feature disabled (MQTT) ###");
    mqttPublishStatus("Door locking is disabled. Enable it via CLI: set locking_enabled true");
    return;
  }
  Serial.println("### Unlock Doors! (MQTT) ###");
  logSequenceStart();
  startSequence(CanSequence::UNLOCK_DOORS, millis());
}

void handleChargeOn() {
  Serial.println("### Charge ON! ###");
  logSequenceStart();
  startSequence(CanSequence::CHARGE_ON, millis());
  server.sendHeader("Location", "/"); 
  server.send(303);
}

void handleUnlock() {
  if (!getLockingEnabled()) {
    Serial.println("### Unlock Doors: Feature disabled ###");
    server.send(403, "text/plain", "Door locking is disabled. Enable it via CLI: set locking_enabled true");
    return;
  }
  Serial.println("### Unlock Doors! ###");

  logSequenceStart();
  startSequence(CanSequence::UNLOCK_DOORS, millis());

  server.sendHeader("Location", "/"); server.send(303);
}

void handleLock() {
  if (!getLockingEnabled()) {
    Serial.println("### Lock Doors: Feature disabled ###");
    server.send(403, "text/plain", "Door locking is disabled. Enable it via CLI: set locking_enabled true");
    return;
  }
  Serial.println("### Lock Doors! ###");

  logSequenceStart();
  startSequence(CanSequence::LOCK_DOORS, millis());

  server.sendHeader("Location", "/"); server.send(303);
}

void handleMqttChargeOn() {
  Serial.println("### Charge ON! (MQTT) ###");
  logSequenceStart();
  startSequence(CanSequence::CHARGE_ON, millis());
}

void connectWiFi(bool fallback) {
  usingFallback = fallback;
  wasStaConnected = false;
  wifiAttemptStart = millis();
  systemLog("[WiFi] Connecting to %s: %s", fallback ? "fallback" : "primary",
                fallback ? getFallbackSSID() : getWifiSSID());
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
  WiFi.begin(fallback ? getFallbackSSID() : getWifiSSID(),
             fallback ? getFallbackPassword() : getWifiPassword());
}

void manageWiFi() {
  if(!wifiEnabled) return;
  bool staEnabled = strlen(getWifiSSID()) > 0;
  bool fallbackEnabled = staEnabled && strlen(getFallbackSSID()) > 0;
  static bool wifiAvailable = false;

  if (!staEnabled) {
    // AP-only mode, no STA connection to manage
    if (!wifiAvailable) {
      Serial.println("\nAP-only mode active!");
      
      if (!MDNS.begin(getHostName())) {
        Serial.println("Error starting mDNS service!");
      } else {
        Serial.println("mDNS service started.");
        MDNS.addService("http", "tcp", 80);
      }
      if (!NBNS.begin(getHostName())) {
        Serial.println("Error starting NBNS service!");
      } else {
        Serial.println("NBNS service started.");
      }

      Serial.print("You can find the UI in the browser at: http://");
      Serial.print(getHostName());
      Serial.print(".local or http://");
      Serial.print(getHostName());
      Serial.println(" or by AP IP address (usually 192.168.4.1):");
      Serial.println(WiFi.softAPIP());
      wifiAvailable = true;
    }
    return;
  }

  static unsigned long previousAttemptTime = 0;
  const unsigned long retryInterval = 10000;
  unsigned long currentTime = millis();

  if (fallbackEnabled) {
    if (WiFi.status() == WL_CONNECTED &&
        (wasStaConnected || WiFi.SSID() == (usingFallback ? getFallbackSSID() : getWifiSSID()))) {
      if (!wasStaConnected) {
        systemLog("[WiFi] Connected to %s", usingFallback ? "fallback" : "primary");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        wasStaConnected = true;
        lastPrimaryScan = currentTime - 30000;
      }

      if (usingFallback) {
        if (primaryScanRunning) {
          int networks = WiFi.scanComplete();
          if (networks >= 0) {
            bool foundPrimary = false;
            for (int i = 0; i < networks; ++i) {
              if (WiFi.SSID(i) == getWifiSSID()) {
                foundPrimary = true;
                break;
              }
            }
            WiFi.scanDelete();
            primaryScanRunning = false;
            if (foundPrimary) {
              if (primarySeen && currentTime - primarySeenSince >= 5000) {
                systemLog("[WiFi] Primary network available again");
                systemLog("[WiFi] Switching to primary");
                primarySeen = false;
                connectWiFi(false);
              } else if (!primarySeen) {
                primarySeen = true;
                primarySeenSince = currentTime;
              }
            } else {
              primarySeen = false;
            }
          } else if (networks != WIFI_SCAN_RUNNING) {
            primaryScanRunning = false;
            primarySeen = false;
          }
        } else if (currentTime - lastPrimaryScan >= (primarySeen ? 5000UL : 30000UL)) {
          WiFi.scanNetworks(true);
          primaryScanRunning = true;
          lastPrimaryScan = currentTime;
        }
      }
    } else if (wasStaConnected) {
      wasStaConnected = false;
      primaryScanRunning = false;
      primarySeen = false;
      // Give primary a fresh connection attempt before falling back.
      connectWiFi(false);
    } else if (currentTime - wifiAttemptStart >= retryInterval) {
      systemLog("[WiFi] %s connection failed", usingFallback ? "Fallback" : "Primary");
      connectWiFi(!usingFallback);
    }
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    if (wifiAvailable) {
      systemLog("[WiFi] Disconnected, stopping mDNS/NBNS services");
      MDNS.end();
      NBNS.end();
      wifiAvailable = false;
    }

    if (currentTime - previousAttemptTime >= retryInterval) {
      Serial.println("WiFi connection missing, waiting for auto-reconnect...");
      previousAttemptTime = currentTime;
    }
  }
  else if (!wifiAvailable) {
    systemLog("[WiFi] Connected; IP: %s", WiFi.localIP().toString().c_str());
  
    if (!MDNS.begin(getHostName())) {
      Serial.println("Error starting mDNS service!");
    } else {
      Serial.println("mDNS service started.");
      MDNS.addService("http", "tcp", 80);
    }
    if (!NBNS.begin(getHostName())) {
      Serial.println("Error starting NBNS service!");
    } else {
      Serial.println("NBNS service started.");
    }

    Serial.print("You can find the UI in the browser at: http://");
    Serial.print(getHostName());
    Serial.print(".local or http://");
    Serial.print(getHostName());
    Serial.println(" or by IP address:");
    Serial.println(WiFi.localIP());
    wifiAvailable = true;
  }
}

void setup() {
  Serial.begin(115200);
  initConfiguration();
  setupSystemLog();

  bool staEnabled = strlen(getWifiSSID()) > 0;
  bool apEnabled = strlen(getApSSID()) > 0;

  if (apEnabled) {
      size_t apPassLen = strlen(getApPassword());
      if (apPassLen > 0 && apPassLen < 8) {
          Serial.println("Error: AP password is too short (must be >= 8 chars). AP mode will be disabled.");
          apEnabled = false;
      }
  }
  
  if (staEnabled && apEnabled) {
      WiFi.mode(WIFI_AP_STA);
      if (strlen(getApPassword()) > 0) {
          WiFi.softAP(getApSSID(), getApPassword());
      } else {
          WiFi.softAP(getApSSID());
      }
      WiFi.setAutoReconnect(strlen(getFallbackSSID()) == 0);
      connectWiFi(false);
      WiFi.setSleep(false);
      wifiEnabled = true;
  } else if (staEnabled) {
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(strlen(getFallbackSSID()) == 0);
      connectWiFi(false);
      WiFi.setSleep(false);
      wifiEnabled = true;
  } else if (apEnabled) {
      WiFi.mode(WIFI_AP);
      if (strlen(getApPassword()) > 0) {
          WiFi.softAP(getApSSID(), getApPassword());
      } else {
          WiFi.softAP(getApSSID());
      }
      WiFi.setSleep(false);
      wifiEnabled = true;
  }
  else {
    Serial.println("WiFi disabled");
    wifiEnabled = false;
  }

  setupMQTT();
  setupCAN();
  setupConnectivity();

  if (wifiEnabled) {
      server.on("/", []() { if (authorizeWebUI()) handleRoot(); });
      server.on("/refresh", []() { if (authorizeWebUI()) handleRefresh(); });
      server.on("/hvac_on", []() { if (authorizeWebUI()) handleHvacOn(); });
      server.on("/hvac_off", []() { if (authorizeWebUI()) handleHvacOff(); });
      server.on("/charge_on", []() { if (authorizeWebUI()) handleChargeOn(); });
      server.on("/unlock", []() { if (authorizeWebUI()) handleUnlock(); });
      server.on("/lock", []() { if (authorizeWebUI()) handleLock(); });
      server.begin();
  }
}

void loop() {
  server.handleClient();
  manageMQTT();
  readAndHandleCANMessage();
  CanSeqResult seqRes = manageCANSequence(millis());
  if (seqRes == CanSeqResult::WAKE_SUCCESS) {
      Serial.println("### Car is awake! ###");
  } else if (seqRes == CanSeqResult::WAKE_TIMEOUT) {
      Serial.println("### Car wake timeout! ###");
  } else if (seqRes == CanSeqResult::SEQUENCE_FINISHED) {
      Serial.println("### Sequence Complete ###");
  }
  manageWiFi();
  processSerialInput();
  manageRemoteConfig();
}

/*
   ***********  Wemos D1 mini (ESP8266) – Settings Configuration  ***********

   - Creates AP "WATER_SENSOR_XXXXXX", pwd "1234".
   - Serves http://192.168.4.1  → settings page with configuration options.
   - Saves settings to EEPROM and restarts.
   - Hold BOOT/IO0 ≥ 4 seconds => clears config and restarts.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <user_interface.h>
#include <espnow.h>

// Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266)
// Requires: ESP8266WiFi library (bundled with ESP8266 core)

// These will be overridden by config values
const char* DEFAULT_AP_PASS = "HardPassword1234";    // >= 8 chars
const char* DEFAULT_AP_PREFIX = "WATER_SENSOR_";  // AP name prefix

// Optional: set AP IP (default is 192.168.4.1)
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

constexpr uint8_t BTN_PIN = 0;          // GPIO0 = FLASH/BOOT
constexpr uint8_t LED_PIN = 2;          // GPIO2 = built‑in LED (LOW = on)
constexpr uint32_t BTN_HOLD_MS = 4000;
constexpr uint16_t EEPROM_SIZE = 64;      // EEPROM size for Wemos D1 Mini (increased for new settings)

// ESP-NOW constants
constexpr uint8_t WIFI_CH = 1;          // channel used for ESP-NOW
constexpr uint32_t ESP_NOW_RETRY_MS = 1000; // Retry interval for failed sends

/* ───── pin definitions ─────────────────────────────── */
const int TRIG_PIN = D5; // GPIO14
const int ECHO_PIN = D6; // GPIO12
/* ───────────────────────────────────────────────────────────── */

// Configuration structure
struct Config {
  std::array<uint8_t, 6> parentMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Default broadcast MAC
  uint32_t refreshRateMs = 5000; // Default 5 seconds
  float barrelHeightCm = 50.0; // Default 50 cm barrel height
  bool ledEnabled = true; // Default LED enabled
  char ssidPrefix[16] = "WATER_SENSOR_"; // Default SSID prefix
  char wifiPassword[32] = "HardPassword1234"; // Default WiFi password
};

ESP8266WebServer server(80);
Config config;

// Sensor reading variables
float currentDistance = 0.0;
float currentWaterLevel = 0.0;
uint32_t lastSensorRead = 0;

// ESP-NOW variables
bool espNowInitialized = false;
bool espNowSendSuccess = true;
uint32_t lastEspNowSend = 0;
uint32_t lastEspNowRetry = 0;

// ESP-NOW payload structure (same as your working example)
struct Payload { 
  float distance; 
  float waterLevel; 
  float barrelHeight; 
} payload;

/* ---------- helpers ------------------------------------------------------ */
String macToString(const uint8_t* mac) {
  char buf[18];
  sprintf(buf,"%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

bool parseMac(const String& s, std::array<uint8_t,6>& out) {
  return sscanf(s.c_str(),"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &out[0],&out[1],&out[2],&out[3],&out[4],&out[5])==6;
}

void blink(uint8_t n, uint16_t d=150){
  while(n--){ digitalWrite(LED_PIN,LOW); delay(d); digitalWrite(LED_PIN,HIGH); delay(d); }
}

// Function declarations (prototypes)
String getWiFiMac();
String getEspNowMac();

// Save configuration to EEPROM
bool saveConfig(const Config& cfg) {
  EEPROM.begin(EEPROM_SIZE);
  
  // Save parent MAC (6 bytes at address 0)
  for (uint8_t i = 0; i < 6; ++i) {
    EEPROM.write(i, cfg.parentMac[i]);
  }
  
  // Save refresh rate (4 bytes at address 6)
  for (uint8_t i = 0; i < 4; ++i) {
    EEPROM.write(6 + i, (cfg.refreshRateMs >> (i * 8)) & 0xFF);
  }
  
  // Save barrel height (4 bytes at address 10)
  union {
    float f;
    uint32_t i;
  } barrelUnion;
  barrelUnion.f = cfg.barrelHeightCm;
  for (uint8_t i = 0; i < 4; ++i) {
    EEPROM.write(10 + i, (barrelUnion.i >> (i * 8)) & 0xFF);
  }
  
  // Save LED setting (1 byte at address 14)
  EEPROM.write(14, cfg.ledEnabled ? 0x01 : 0x00);
  
  // Save SSID prefix (16 bytes at address 15)
  for (uint8_t i = 0; i < 16; ++i) {
    EEPROM.write(15 + i, cfg.ssidPrefix[i]);
  }
  
  // Save WiFi password (32 bytes at address 31)
  for (uint8_t i = 0; i < 32; ++i) {
    EEPROM.write(31 + i, cfg.wifiPassword[i]);
  }
  
  // Write config marker (1 byte at address 63)
  EEPROM.write(63, 0xAA); // Config marker
  
  bool success = EEPROM.commit();
  EEPROM.end();
  return success;
}

// Load configuration from EEPROM
bool loadConfig(Config& cfg) {
  EEPROM.begin(EEPROM_SIZE);
  
  // Check if config exists (config marker at address 63)
  if (EEPROM.read(63) != 0xAA) {
    EEPROM.end();
    return false; // No config saved
  }
  
  // Load parent MAC (6 bytes at address 0)
  for (uint8_t i = 0; i < 6; ++i) {
    cfg.parentMac[i] = EEPROM.read(i);
  }
  
  // Load refresh rate (4 bytes at address 6)
  cfg.refreshRateMs = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    cfg.refreshRateMs |= ((uint32_t)EEPROM.read(6 + i)) << (i * 8);
  }
  
  // Load barrel height (4 bytes at address 10)
  union {
    float f;
    uint32_t i;
  } barrelUnion;
  barrelUnion.i = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    barrelUnion.i |= ((uint32_t)EEPROM.read(10 + i)) << (i * 8);
  }
  cfg.barrelHeightCm = barrelUnion.f;
  
  // Load LED setting (1 byte at address 14)
  cfg.ledEnabled = (EEPROM.read(14) == 0x01);
  
  // Load SSID prefix (16 bytes at address 15)
  for (uint8_t i = 0; i < 16; ++i) {
    cfg.ssidPrefix[i] = EEPROM.read(15 + i);
  }
  
  // Load WiFi password (32 bytes at address 31)
  for (uint8_t i = 0; i < 32; ++i) {
    cfg.wifiPassword[i] = EEPROM.read(31 + i);
  }
  
  EEPROM.end();
  return true;
}

// Clear configuration
void clearConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (uint8_t i = 0; i < EEPROM_SIZE; ++i) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Convert milliseconds to minutes and seconds
void msToMinSec(uint32_t ms, uint8_t& minutes, uint8_t& seconds) {
  minutes = ms / 60000;
  seconds = (ms % 60000) / 1000;
}

// Convert minutes and seconds to milliseconds
uint32_t minSecToMs(uint8_t minutes, uint8_t seconds) {
  return (minutes * 60 + seconds) * 1000;
}

// ESP-NOW callback function
void onEspNowSend(uint8_t* mac, uint8_t status) {
  espNowSendSuccess = (status == 0);
  
  // Log the MAC address that was sent to
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  if (espNowSendSuccess) {
    Serial.printf("ESP-NOW send: OK → %s\n", macStr);
  } else {
    Serial.printf("ESP-NOW send: FAIL → %s (Error code: %d)\n", macStr, status);
  }
}

// Initialize ESP-NOW
bool initEspNow() {
  Serial.println("=== ESP-NOW INITIALIZATION ===");
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed");
    return false;
  }
  Serial.println("ESP-NOW init: OK");
  
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  Serial.println("ESP-NOW role: CONTROLLER");
  
  esp_now_register_send_cb(onEspNowSend);
  Serial.println("ESP-NOW callback: Registered");
  
  // Check if parent MAC is not default (FF:FF:FF:FF:FF:FF)
  bool isDefaultMac = true;
  for (int i = 0; i < 6; i++) {
    if (config.parentMac[i] != 0xFF) {
      isDefaultMac = false;
      break;
    }
  }
  
  if (isDefaultMac) {
    Serial.println("Parent MAC is default (FF:FF:FF:FF:FF:FF) - ESP-NOW disabled");
    Serial.println("=== ESP-NOW DISABLED ===");
    return false;
  }
  
  Serial.printf("Adding peer: %s\n", macToString(config.parentMac.data()).c_str());
  if (esp_now_add_peer(config.parentMac.data(), ESP_NOW_ROLE_SLAVE, WIFI_CH, NULL, 0) != 0) {
    Serial.println("ESP-NOW add peer failed");
    Serial.println("=== ESP-NOW INIT FAILED ===");
    return false;
  }
  Serial.println("ESP-NOW peer: Added successfully");
  
  Serial.printf("ESP-NOW initialized → parent %s\n", macToString(config.parentMac.data()).c_str());
  Serial.println("=== ESP-NOW READY ===");
  return true;
}

// Send data via ESP-NOW
void sendEspNowData() {
  if (!espNowInitialized) {
    Serial.println("ESP-NOW send: Skipped (not initialized)");
    return;
  }
  
  // Prepare payload
  payload.distance = currentDistance;
  payload.waterLevel = currentWaterLevel;
  payload.barrelHeight = config.barrelHeightCm;
  
  Serial.println("=== ESP-NOW SENDING DATA ===");
  Serial.printf("Target MAC: %s\n", macToString(config.parentMac.data()).c_str());
  Serial.printf("Payload: Distance=%.1f cm, Water=%.1f%%, Barrel=%.1f cm\n", 
                payload.distance, payload.waterLevel, payload.barrelHeight);
  Serial.printf("Payload size: %d bytes\n", sizeof(payload));
  
  // Send data
  uint8_t result = esp_now_send(config.parentMac.data(), (uint8_t*)&payload, sizeof(payload));
  if (result != 0) {
    Serial.printf("ESP-NOW send failed with error code: %d\n", result);
    espNowSendSuccess = false;
  } else {
    Serial.println("ESP-NOW send: Request sent successfully (waiting for callback)");
  }
  
  lastEspNowSend = millis();
}

// Measure distance using ultrasonic sensor
float measureDistanceCM() {
  // Clear the trigger pin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Send 10 microsecond pulse
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Measure the response with timeout (30ms = 30,000 microseconds)
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  Serial.printf("Sensor Debug - Raw duration: %ld microseconds\n", duration);
  
  if (duration == 0) {
    Serial.println("Sensor Debug - Timeout or no echo received");
    return -1; // Timeout/no reading
  }
  
  // Calculate distance in cm (speed of sound = 0.034 cm/microsecond)
  float distance = duration * 0.034 / 2;
  Serial.printf("Sensor Debug - Calculated distance: %.2f cm\n", distance);
  
  return distance;
}

// Calculate water level percentage
float calculateWaterLevel(float distance, float barrelHeight) {
  // If distance is less than 20cm, treat as empty (0%)
  if (distance < 20.0) {
    return 0.0;
  }
  
  // Adjust distance by subtracting the 20cm offset (sensor mounting height)
  float adjustedDistance = distance - 20.0;
  
  // Calculate water level: ((barrel_height - adjusted_distance) / barrel_height) * 100%
  float waterLevel = ((barrelHeight - adjustedDistance) / barrelHeight) * 100.0;
  
  // Clamp between 0% and 100%
  if (waterLevel < 0.0) waterLevel = 0.0;
  if (waterLevel > 100.0) waterLevel = 100.0;
  
  return waterLevel;
}

// Update sensor readings based on refresh rate
void updateSensorReadings() {
  uint32_t currentTime = millis();
  
  // Check if it's time to read the sensor
  if (currentTime - lastSensorRead >= config.refreshRateMs) {
    Serial.println("=== SENSOR READING TRIGGERED ===");
    Serial.printf("Time since last read: %u ms\n", currentTime - lastSensorRead);
    
    currentDistance = measureDistanceCM();
    currentWaterLevel = calculateWaterLevel(currentDistance, config.barrelHeightCm);
    lastSensorRead = currentTime;
    
    // Debug output
    Serial.printf("Sensor Update - Distance: %.1f cm, Water Level: %.1f%%\n", 
                  currentDistance, currentWaterLevel);
    
    // Send data via ESP-NOW if initialized
    if (espNowInitialized) {
      Serial.println("Auto-sending data via ESP-NOW (refresh rate trigger)");
      sendEspNowData();
    }
    
    Serial.println("=== SENSOR READING COMPLETED ===");
  }
}

/* ---------- Web handlers -------------------------------------------------- */
void handleRoot(){
  uint8_t selfMac[6]; WiFi.macAddress(selfMac);
  
  // Check if configuration exists (not default values)
  bool hasConfig = (config.parentMac[0] != 0xFF || config.refreshRateMs != 5000 || config.barrelHeightCm != 50.0 || !config.ledEnabled || 
                   strcmp(config.ssidPrefix, "WATER_SENSOR_") != 0 || strcmp(config.wifiPassword, "HardPassword1234") != 0);
  
  // Convert refresh rate to minutes and seconds
  uint8_t minutes, seconds;
  msToMinSec(config.refreshRateMs, minutes, seconds);
  
  // Update sensor readings if needed
  updateSensorReadings();
  
  if (hasConfig) {
    // Show configured status page
    String html = R"(
    <html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>
    <title>ESP8266 Settings</title>
    <style>
      body { font-family: Arial, sans-serif; margin: 20px; }
      .info { background-color: #e8f5e8; padding: 15px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #4CAF50; }
      .warning { background-color: #fff3cd; padding: 15px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #ffc107; }
             .btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; }
       .btn-primary { background-color: #007bff; color: white; }
       .btn-warning { background-color: #ffc107; color: black; }
       .btn-success { background-color: #28a745; color: white; }
       .btn:hover { opacity: 0.8; }
       .sensor { background-color: #e3f2fd; padding: 15px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #2196F3; }
       .mac-info { background-color: #f8f9fa; padding: 10px; margin: 10px 0; border-radius: 3px; font-family: monospace; }
    </style>
    </head><body>
    <h2>ESP8266 Distance Sensor Settings</h2>
    
    <div class="info">
      <p><b>Device MAC Addresses:</b></p>
      <div class="mac-info">
        <strong>WiFi MAC:</strong> %WIFI_MAC%<br>
        <strong>ESP-NOW MAC:</strong> %ESPNOW_MAC% (Use this for ESP-NOW configuration)
      </div>
      <p><b>Status:</b>Wemos is configured already</p>
    </div>
    
         <div class="warning">
       <p><b>Current Configuration:</b></p>
       <ul>
                   <li><b>Parent MAC:</b> %PARENT_MAC%</li>
          <li><b>Refresh Rate:</b> %MINUTES%m %SECONDS%s</li>
          <li><b>Barrel Height:</b> %BARREL_HEIGHT% cm</li>
          <li><b>LED Status:</b> %LED_STATUS%</li>
          <li><b>WiFi SSID:</b> %SSID_PREFIX%XXXXXX</li>
          <li><b>WiFi Password:</b> %WIFI_PASSWORD%</li>
          <li><b>ESP-NOW Status:</b> %ESPNOW_STATUS%</li>
        </ul>
     </div>
    
          <div class="sensor">
       <p><b>Current Water Level:</b></p>
       <div style="font-size: 32px; font-weight: bold; color: #1976D2; text-align: center; margin: 10px 0;">
         %WATER_LEVEL%%
       </div>
       <p style="text-align: center; margin: 5px 0; color: #666;">
         <small>Distance: %SENSOR_DISTANCE% cm | Barrel Height: %BARREL_HEIGHT% cm</small>
       </p>
     </div>
     
        <p><b>What would you like to do?</b></p>
     
     <a href="/update" class="btn btn-primary">Update Settings</a>
     <a href="/reset" class="btn btn-warning">Reset to Default</a>
     <a href="/sensor" class="btn btn-success">View Water Level</a>
     <a href="/debugmac" class="btn btn-primary">Debug MAC Addresses</a>
     
         </body></html>)";
    
    html.replace("%WIFI_MAC%", getWiFiMac());
    html.replace("%ESPNOW_MAC%", getEspNowMac());
    html.replace("%PARENT_MAC%", macToString(config.parentMac.data()));
    html.replace("%MINUTES%", String(minutes));
    html.replace("%SECONDS%", String(seconds));
    html.replace("%BARREL_HEIGHT%", String((int)config.barrelHeightCm));
    html.replace("%SENSOR_DISTANCE%", String(currentDistance, 1));
         html.replace("%WATER_LEVEL%", String(currentWaterLevel, 1));
     html.replace("%LED_STATUS%", config.ledEnabled ? "Enabled" : "Disabled");
           html.replace("%SSID_PREFIX%", String(config.ssidPrefix));
      html.replace("%WIFI_PASSWORD%", String(config.wifiPassword));
      html.replace("%ESPNOW_STATUS%", espNowInitialized ? (espNowSendSuccess ? "Connected" : "Error") : "Disabled (Parent MAC not configured)");
       
      server.send(200,"text/html",html);
  } else {
    // Show initial configuration page
    String html = R"(
    <html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>
    <title>ESP8266 Settings</title>
    <style>
      body { font-family: Arial, sans-serif; margin: 20px; }
      .form-group { margin-bottom: 15px; }
      label { display: block; margin-bottom: 5px; font-weight: bold; }
      input[type="text"], input[type="number"] { width: 200px; padding: 5px; }
      input[type="submit"] { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; }
      input[type="submit"]:hover { background-color: #45a049; }
      .info { background-color: #f0f0f0; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
      .sensor { background-color: #e3f2fd; padding: 15px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #2196F3; }
      .mac-info { background-color: #f8f9fa; padding: 10px; margin: 10px 0; border-radius: 3px; font-family: monospace; }
    </style>
    </head><body>
    <h2>ESP8266 Distance Sensor Settings</h2>
    
    <div class="info">
      <p><b>Device MAC Addresses:</b></p>
      <div class="mac-info">
        <strong>WiFi MAC:</strong> %WIFI_MAC%<br>
        <strong>ESP-NOW MAC:</strong> %ESPNOW_MAC% (Use this for ESP-NOW configuration)
      </div>
      <p><b>Status:</b>Initial configuration required</p>
    </div>
    
         <div class="sensor">
       <p><b>Current Water Level:</b></p>
       <div style="font-size: 32px; font-weight: bold; color: #1976D2; text-align: center; margin: 10px 0;">
         %WATER_LEVEL%%
       </div>
       <p style="text-align: center; margin: 5px 0; color: #666;">
         <small>Distance: %SENSOR_DISTANCE% cm | Barrel Height: %BARREL_HEIGHT% cm</small>
       </p>
     </div>
     
    <form action='/save' method='post'>
      <div class="form-group">
        <label for="pmac">Parent MAC Address:</label>
        <input type="text" id="pmac" name="pmac" value="%PARENT_MAC%" placeholder="FF:FF:FF:FF:FF:FF">
      </div>
      
      <div class="form-group">
        <label for="minutes">Refresh Rate:</label>
        <input type="number" id="minutes" name="minutes" value="%MINUTES%" min="0" max="59" style="width: 80px;"> minutes
        <input type="number" id="seconds" name="seconds" value="%SECONDS%" min="0" max="59" style="width: 80px;"> seconds
      </div>
      
                     <div class="form-group">
          <label for="barrel">Barrel Height (cm):</label>
          <input type="number" id="barrel" name="barrel" value="%BARREL_HEIGHT%" min="1" max="1000" step="1">
        </div>
        
                <div class="form-group">
          <label for="led">
            <input type="checkbox" id="led" name="led" %LED_CHECKED%>
            Enable LED blinking (indicates device is working)
          </label>
        </div>
        
        <div class="form-group">
          <label for="ssid">WiFi SSID Prefix:</label>
          <input type="text" id="ssid" name="ssid" value="%SSID_PREFIX%" placeholder="WATER_SENSOR_" maxlength="15">
          <small>SSID will be: [prefix]XXXXXX (where XXXXXX is device MAC)</small>
        </div>
        
        <div class="form-group">
          <label for="password">WiFi Password:</label>
          <input type="text" id="password" name="password" value="%WIFI_PASSWORD%" placeholder="HardPassword1234" minlength="8" maxlength="31">
          <small>Must be at least 8 characters long</small>
        </div>
        
                <input type="submit" value="Save Settings & Reboot">
    </form>
    </body></html>)";
    
    html.replace("%WIFI_MAC%", getWiFiMac());
    html.replace("%ESPNOW_MAC%", getEspNowMac());
    html.replace("%PARENT_MAC%", macToString(config.parentMac.data()));
    html.replace("%MINUTES%", String(minutes));
    html.replace("%SECONDS%", String(seconds));
    html.replace("%BARREL_HEIGHT%", String((int)config.barrelHeightCm));
    html.replace("%SENSOR_DISTANCE%", String(currentDistance, 1));
    html.replace("%WATER_LEVEL%", String(currentWaterLevel, 1));
    html.replace("%LED_CHECKED%", config.ledEnabled ? "checked" : "");
    html.replace("%SSID_PREFIX%", String(config.ssidPrefix));
    html.replace("%WIFI_PASSWORD%", String(config.wifiPassword));
    
    server.send(200,"text/html",html);
  }
}

void handleSave(){
  if(!server.hasArg("pmac") || !server.hasArg("minutes") || !server.hasArg("seconds") || !server.hasArg("barrel")){ 
    server.send(400,"text/plain","Missing parameters"); 
    return;
  }
  
  // Parse parent MAC
  String macStr = server.arg("pmac");
  if(!parseMac(macStr, config.parentMac)){ 
    server.send(400,"text/plain","Bad MAC format"); 
    return;
  }
  
  // Parse refresh rate
  uint8_t minutes = server.arg("minutes").toInt();
  uint8_t seconds = server.arg("seconds").toInt();
  if(minutes > 59 || seconds > 59) {
    server.send(400,"text/plain","Invalid time format");
    return;
  }
  config.refreshRateMs = minSecToMs(minutes, seconds);
  
  // Parse barrel height
  int barrel = server.arg("barrel").toInt();
  if(barrel <= 0 || barrel > 1000) {
    server.send(400,"text/plain","Invalid barrel height");
    return;
  }
  config.barrelHeightCm = (float)barrel;
  
  // Parse LED setting (checkbox - if present, LED is enabled)
  config.ledEnabled = server.hasArg("led");
  
  // Parse SSID prefix
  String ssidStr = server.arg("ssid");
  if(ssidStr.length() > 0 && ssidStr.length() <= 15) {
    strcpy(config.ssidPrefix, ssidStr.c_str());
  }
  
  // Parse WiFi password
  String passwordStr = server.arg("password");
  if(passwordStr.length() >= 8 && passwordStr.length() <= 31) {
    strcpy(config.wifiPassword, passwordStr.c_str());
  } else if(passwordStr.length() > 0) {
    server.send(400,"text/plain","WiFi password must be 8-31 characters long");
    return;
  }
  
  // Save configuration
  if(saveConfig(config)) {
    server.send(200,"text/plain","Settings saved. Rebooting...");
    delay(800);
    ESP.restart();
  } else {
    server.send(500,"text/plain","Failed to save settings");
  }
}

void handleUpdate(){
  uint8_t selfMac[6]; WiFi.macAddress(selfMac);
  
  // Convert refresh rate to minutes and seconds
  uint8_t minutes, seconds;
  msToMinSec(config.refreshRateMs, minutes, seconds);
  
  String html = R"(
  <html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>
  <title>ESP8266 Settings - Update</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type="text"], input[type="number"] { width: 200px; padding: 5px; }
    input[type="submit"] { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; }
    input[type="submit"]:hover { background-color: #45a049; }
    .info { background-color: #f0f0f0; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
    .btn { display: inline-block; padding: 8px 16px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; }
    .btn-secondary { background-color: #6c757d; color: white; }
    .btn:hover { opacity: 0.8; }
    .mac-info { background-color: #f8f9fa; padding: 10px; margin: 10px 0; border-radius: 3px; font-family: monospace; }
  </style>
  </head><body>
  <h2>ESP8266 Distance Sensor Settings - Update</h2>
  
  <div class="info">
    <p><b>Device MAC Addresses:</b></p>
    <div class="mac-info">
      <strong>WiFi MAC:</strong> %WIFI_MAC%<br>
      <strong>ESP-NOW MAC:</strong> %ESPNOW_MAC% (Use this for ESP-NOW configuration)
    </div>
    <p><b>Status:</b>Updating configuration</p>
  </div>
  
  <form action='/save' method='post'>
    <div class="form-group">
      <label for="pmac">Parent MAC Address:</label>
      <input type="text" id="pmac" name="pmac" value="%PARENT_MAC%" placeholder="FF:FF:FF:FF:FF:FF">
    </div>
    
    <div class="form-group">
      <label for="minutes">Refresh Rate:</label>
      <input type="number" id="minutes" name="minutes" value="%MINUTES%" min="0" max="59" style="width: 80px;"> minutes
      <input type="number" id="seconds" name="seconds" value="%SECONDS%" min="0" max="59" style="width: 80px;"> seconds
    </div>
    
                         <div class="form-group">
        <label for="barrel">Barrel Height (cm):</label>
        <input type="number" id="barrel" name="barrel" value="%BARREL_HEIGHT%" min="1" max="1000" step="1">
      </div>
      
      <div class="form-group">
        <label for="led">
          <input type="checkbox" id="led" name="led" %LED_CHECKED%>
          Enable LED blinking (indicates device is working)
        </label>
      </div>
      
      <div class="form-group">
        <label for="ssid">WiFi SSID Prefix:</label>
        <input type="text" id="ssid" name="ssid" value="%SSID_PREFIX%" placeholder="WATER_SENSOR_" maxlength="15">
        <small>SSID will be: [prefix]XXXXXX (where XXXXXX is device MAC)</small>
      </div>
      
      <div class="form-group">
        <label for="password">WiFi Password:</label>
        <input type="text" id="password" name="password" value="%WIFI_PASSWORD%" placeholder="HardPassword1234" minlength="8" maxlength="31">
        <small>Must be at least 8 characters long</small>
      </div>
      
            <input type="submit" value="Update Settings & Reboot">
    <a href="/" class="btn btn-secondary">Cancel</a>
  </form>
  </body></html>)";
  
  html.replace("%WIFI_MAC%", getWiFiMac());
  html.replace("%ESPNOW_MAC%", getEspNowMac());
  html.replace("%PARENT_MAC%", macToString(config.parentMac.data()));
     html.replace("%MINUTES%", String(minutes));
   html.replace("%SECONDS%", String(seconds));
       html.replace("%BARREL_HEIGHT%", String((int)config.barrelHeightCm));
    html.replace("%LED_CHECKED%", config.ledEnabled ? "checked" : "");
    html.replace("%SSID_PREFIX%", String(config.ssidPrefix));
    html.replace("%WIFI_PASSWORD%", String(config.wifiPassword));
    
    server.send(200,"text/html",html);
}

void handleReset(){
  // Reset configuration to defaults
  config.parentMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  config.refreshRateMs = 5000;
  config.barrelHeightCm = 50.0;
  config.ledEnabled = true;
  strcpy(config.ssidPrefix, "WATER_SENSOR_");
  strcpy(config.wifiPassword, "HardPassword1234");
  
  // Clear EEPROM
  clearConfig();
  
  // Send confirmation page
  String html = R"(
  <html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>
  <title>ESP8266 Settings - Reset</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .success { background-color: #d4edda; padding: 15px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #28a745; }
    .btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; }
    .btn-primary { background-color: #007bff; color: white; }
    .btn:hover { opacity: 0.8; }
  </style>
  </head><body>
  <h2>ESP8266 Distance Sensor Settings - Reset</h2>
  
  <div class="success">
    <p><b>Settings Reset Successfully!</b></p>
    <p>All configuration has been cleared and reset to default values.</p>
    <ul>
             <li><b>Parent MAC:</b> FF:FF:FF:FF:FF:FF (Broadcast)</li>
       <li><b>Refresh Rate:</b> 0m 5s</li>
       <li><b>Barrel Height:</b> 50 cm</li>
       <li><b>LED Status:</b> Enabled</li>
       <li><b>WiFi SSID:</b> WATER_SENSOR_XXXXXX</li>
       <li><b>WiFi Password:</b> HardPassword1234</li>
     </ul>
  </div>
  
  <p>The device will now use default settings on next boot.</p>
  
  <a href="/" class="btn btn-primary">Back to Settings</a>
  
  </body></html>)";
  
  server.send(200,"text/html",html);
}

// Handle immediate sensor reading endpoint
void handleReadSensor() {
  Serial.println("=== MANUAL SENSOR READING TRIGGERED ===");
  
  // Force immediate sensor reading
  currentDistance = measureDistanceCM();
  currentWaterLevel = calculateWaterLevel(currentDistance, config.barrelHeightCm);
  lastSensorRead = millis();
  
  // Debug output
  Serial.printf("Manual sensor reading - Distance: %.1f cm, Water Level: %.1f%%\n", 
                currentDistance, currentWaterLevel);
  
  // Send data via ESP-NOW if initialized
  if (espNowInitialized) {
    Serial.println("Manual-sending data via ESP-NOW (button trigger)");
    sendEspNowData();
  }
  
  // Return JSON response
  String json = "{\"distance\":" + String(currentDistance, 1) + 
                ",\"waterLevel\":" + String(currentWaterLevel, 1) + 
                ",\"barrelHeight\":" + String((int)config.barrelHeightCm) + "}";
  
  Serial.printf("Sending JSON response: %s\n", json.c_str());
  Serial.println("=== MANUAL SENSOR READING COMPLETED ===");
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "application/json", json);
}

// Debug endpoint to test different MAC addresses
void handleDebugMac() {
  String html = R"(
  <html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>
  <title>ESP8266 MAC Address Debug</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .mac-info { background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; font-family: monospace; }
    .btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; }
    .btn-primary { background-color: #007bff; color: white; }
    .btn-success { background-color: #28a745; color: white; }
    .btn:hover { opacity: 0.8; }
  </style>
  </head><body>
  <h2>ESP8266 MAC Address Debug</h2>
  
  <div class="mac-info">
    <h3>All Available MAC Addresses:</h3>
    <p><strong>WiFi MAC:</strong> %WIFI_MAC%</p>
    <p><strong>STATION_IF MAC:</strong> %STATION_MAC%</p>
    <p><strong>SOFTAP_IF MAC:</strong> %SOFTAP_MAC%</p>
    <p><strong>User Interface 0 MAC:</strong> %USER_MAC%</p>
  </div>
  
  <div class="mac-info">
    <h3>Instructions:</h3>
    <p>1. Check your parent device to see which MAC address it reports receiving data from</p>
    <p>2. Compare it with the MAC addresses listed above</p>
    <p>3. Use the matching MAC address for ESP-NOW configuration</p>
  </div>
  
  <div style='text-align: center;'>
    <a href='/' class='btn btn-primary'>Back to Settings</a>
    <button onclick='sendTestData()' class='btn btn-success'>Send Test ESP-NOW Data</button>
  </div>
  
  <script>
  function sendTestData() {
    fetch('/read')
      .then(response => response.json())
      .then(data => {
        alert('Test data sent! Check your parent device to see which MAC address it reports.');
      })
      .catch(error => {
        alert('Error sending test data: ' + error);
      });
  }
  </script>
  
  </body></html>)";
  
  // Get all MAC addresses
  uint8_t mac[6];
  
  wifi_get_macaddr(STATION_IF, mac);
  String stationMac = macToString(mac);
  
  wifi_get_macaddr(SOFTAP_IF, mac);
  String softapMac = macToString(mac);
  
  WiFi.macAddress(mac);
  String wifiMac = macToString(mac);
  
  uint8_t userMac[6];
  wifi_get_macaddr(0, userMac);
  String userMacStr = macToString(userMac);
  
  html.replace("%WIFI_MAC%", wifiMac);
  html.replace("%STATION_MAC%", stationMac);
  html.replace("%SOFTAP_MAC%", softapMac);
  html.replace("%USER_MAC%", userMacStr);
  
  server.send(200, "text/html", html);
}

// Handle sensor reading endpoint
void handleSensor() {
  // Update sensor readings if needed
  updateSensorReadings();
  
  // Convert refresh rate to display text
  uint8_t minutes, seconds;
  msToMinSec(config.refreshRateMs, minutes, seconds);
  String refreshRateText;
  if (minutes > 0) {
    refreshRateText = String(minutes) + "m " + String(seconds) + "s";
  } else {
    refreshRateText = String(seconds) + "s";
  }
  
  String html = "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>";
  html += "<title>ESP8266 Water Level Sensor - Live Reading</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += ".sensor { background-color: #e3f2fd; padding: 20px; margin-bottom: 20px; border-radius: 5px; border-left: 4px solid #2196F3; }";
  html += ".value { font-size: 48px; font-weight: bold; color: #1976D2; text-align: center; margin: 20px 0; }";
  html += ".btn { display: inline-block; padding: 10px 20px; margin: 5px; text-decoration: none; border-radius: 5px; font-weight: bold; }";
  html += ".btn-primary { background-color: #007bff; color: white; }";
  html += ".btn-success { background-color: #28a745; color: white; }";
  html += ".btn:hover { opacity: 0.8; }";
  html += ".info { background-color: #f0f0f0; padding: 10px; margin-bottom: 20px; border-radius: 5px; }";
  html += "</style>";
  html += "<script>";
  html += "function refreshReading() {";
  html += "  console.log('Refresh button clicked - fetching new data...');";
  html += "  document.getElementById('refreshBtn').textContent = 'Refreshing...';";
  html += "  document.getElementById('refreshBtn').disabled = true;";
  html += "  fetch('/read')";
  html += "    .then(response => {";
  html += "      console.log('Response status:', response.status);";
  html += "      return response.json();";
  html += "    })";
  html += "    .then(data => {";
  html += "      console.log('Received data:', data);";
  html += "      document.getElementById('waterLevel').textContent = data.waterLevel + '%';";
  html += "      document.getElementById('distance').textContent = data.distance + ' cm';";
  html += "      document.getElementById('barrelHeight').textContent = data.barrelHeight + ' cm';";
  html += "      document.getElementById('refreshBtn').textContent = 'Refresh Reading';";
  html += "      document.getElementById('refreshBtn').disabled = false;";
  html += "      console.log('Data updated successfully');";
  html += "    })";
  html += "    .catch(error => {";
  html += "      console.error('Error fetching sensor data:', error);";
  html += "      document.getElementById('refreshBtn').textContent = 'Error - Click to Retry';";
  html += "      document.getElementById('refreshBtn').disabled = false;";
  html += "    });";
  html += "}";
  html += "setTimeout(refreshReading, " + String(config.refreshRateMs) + ");";
  html += "</script>";
  html += "</head><body>";
  html += "<h2>ESP8266 Water Level Sensor - Live Reading</h2>";
  html += "<div class='info'>";
  html += "<p><b>Current Water Level Reading:</b></p>";
  if (espNowInitialized) {
    html += "<p style='color: #28a745;'><b>ESP-NOW Status:</b> " + String(espNowSendSuccess ? "Connected" : "Error") + "</p>";
  } else {
    html += "<p style='color: #dc3545;'><b>ESP-NOW Status:</b> Disabled (Parent MAC not configured)</p>";
  }
  html += "</div>";
  html += "<div class='sensor'>";
  html += "<div class='value' id='waterLevel'>" + String(currentWaterLevel, 1) + "%</div>";
  html += "<p style='text-align: center; margin: 0;'>Water Level | Distance: <span id='distance'>" + String(currentDistance, 1) + "</span> cm | Barrel Height: <span id='barrelHeight'>" + String((int)config.barrelHeightCm) + "</span> cm</p>";
  html += "</div>";
  html += "<div style='text-align: center;'>";
  html += "<a href='/' class='btn btn-primary'>Back to Settings</a>";
  html += "<button onclick='refreshReading()' class='btn btn-success' id='refreshBtn'>Refresh Reading</button>";
  html += "</div>";
  html += "<p style='text-align: center; margin-top: 20px; color: #666;'>";
  html += "<small>Auto-refreshing every " + refreshRateText + "</small>";
  html += "</p>";
  html += "</body></html>";
  
  server.send(200,"text/html",html);
}

// Get the actual MAC address that ESP-NOW uses
String getEspNowMac() {
  uint8_t mac[6];
  
  // Try different methods to get the MAC address
  // Method 1: Get from STATION interface
  wifi_get_macaddr(STATION_IF, mac);
  String stationMac = macToString(mac);
  
  // Method 2: Get from SOFTAP interface
  wifi_get_macaddr(SOFTAP_IF, mac);
  String softapMac = macToString(mac);
  
  // Method 3: Get from WiFi library
  WiFi.macAddress(mac);
  String wifiMac = macToString(mac);
  
  // Method 4: Get from user_interface (ESP8266 specific)
  uint8_t userMac[6];
  wifi_get_macaddr(0, userMac); // Interface 0
  String userMacStr = macToString(userMac);
  
  // Log all MAC addresses for debugging
  Serial.println("=== MAC ADDRESS DEBUG ===");
  Serial.printf("STATION_IF MAC: %s\n", stationMac.c_str());
  Serial.printf("SOFTAP_IF MAC: %s\n", softapMac.c_str());
  Serial.printf("WiFi.macAddress(): %s\n", wifiMac.c_str());
  Serial.printf("User Interface 0 MAC: %s\n", userMacStr.c_str());
  Serial.println("=========================");
  
  // Return the STATION interface MAC as it's most likely used for ESP-NOW
  return stationMac;
}

// Get the MAC address that WiFi uses
String getWiFiMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  return macToString(mac);
}

/* ---------- button long‑press reset -------------------------------------- */
void checkButton(){
  static uint32_t t0=0;
  static bool pressed = false;
  
  if(digitalRead(BTN_PIN)==LOW){
    if(!t0) {
      t0 = millis();
      pressed = true;
    }
    else if(millis()-t0>=BTN_HOLD_MS){
      Serial.println("Long press → clearing config");
      clearConfig();
      blink(3,100);
      ESP.restart();
    }
  } else if(pressed && t0 > 0) {
    pressed = false;
    t0 = 0;
  }
}

void setup() {
  Serial.begin(74880);  // Standard ESP8266 baud rate
  delay(200);
  
  Serial.println("\n\n=== ESP8266 Configuration Mode Starting ===");
  

  
  // Initialize pins
  pinMode(LED_PIN,OUTPUT); 
  digitalWrite(LED_PIN,HIGH);
  pinMode(BTN_PIN,INPUT_PULLUP);
  
  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  Serial.println("Pins initialized");
  
  // Load configuration (if exists)
  bool configLoaded = loadConfig(config);
  Serial.printf("Config loaded: %s\n", configLoaded ? "YES" : "NO");

  // Clean WiFi setup (same as working example)
  Serial.println("Setting up WiFi...");
  WiFi.persistent(false);   // don't write Wi-Fi settings to flash
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_AP);

  // Configure AP IP (optional)
  Serial.println("Configuring AP IP...");
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // NOW get MAC address after WiFi is initialized
  uint8_t mac[6]; 
  WiFi.macAddress(mac);
  char ssid[32]; 
  sprintf(ssid,"%s%02X%02X%02X",config.ssidPrefix,mac[3],mac[4],mac[5]);
  
  Serial.printf("Device MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("WiFi MAC: %s\n", getWiFiMac().c_str());
  Serial.printf("ESP-NOW MAC: %s\n", getEspNowMac().c_str());
  Serial.printf("Generated SSID: %s\n", ssid);
  Serial.printf("AP_PASS: %s\n", config.wifiPassword);

  // Start AP: channel=1, hidden=0 (visible), max_conn=4
  Serial.println("Starting AP...");
  bool ok = WiFi.softAP(ssid, config.wifiPassword, 1, 0, 4);

  Serial.println(ok ? "AP started" : "AP failed");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("Password: "); Serial.println(config.wifiPassword);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  
  // Verify AP is working
  Serial.println("=== AP VERIFICATION ===");
  Serial.printf("Current SSID: %s\n", WiFi.softAPSSID().c_str());
  Serial.printf("Current Password: %s\n", WiFi.softAPPSK().c_str());
  Serial.printf("AP Status: %s\n", WiFi.softAPgetStationNum() >= 0 ? "RUNNING" : "ERROR");
  Serial.printf("AP Mode: %s\n", WiFi.getMode() == WIFI_AP ? "AP MODE" : "WRONG MODE");
  Serial.printf("AP Channel: %d\n", WiFi.channel());
  Serial.println("=======================");
  
     // If AP failed, try alternative approach
   if (!ok) {
     Serial.println("Trying alternative AP setup...");
     WiFi.softAP(ssid, config.wifiPassword);  // Try without extra parameters
     delay(1000);
     Serial.printf("Alternative SSID: %s\n", WiFi.softAPSSID().c_str());
     Serial.printf("Alternative Password: %s\n", WiFi.softAPPSK().c_str());
   }
  
  // Setup web server
  server.on("/",handleRoot);
  server.on("/save",HTTP_POST,handleSave);
  server.on("/update",handleUpdate);
  server.on("/reset",handleReset);
  server.on("/sensor",handleSensor);
  server.on("/read",handleReadSensor);
  server.on("/debugmac", handleDebugMac); // Add the new debug endpoint
  server.begin();
  Serial.println("Web server started");
  
  // Initialize ESP-NOW
  Serial.println("Initializing ESP-NOW...");
  espNowInitialized = initEspNow();
  
  if (espNowInitialized) {
    Serial.println("ESP-NOW: ENABLED and ready to send data");
  } else {
    Serial.println("ESP-NOW: DISABLED (no valid parent MAC configured)");
  }
  
  // Initialize sensor readings
  currentDistance = measureDistanceCM();
  currentWaterLevel = calculateWaterLevel(currentDistance, config.barrelHeightCm);
  lastSensorRead = millis();
  Serial.printf("Initial sensor reading - Distance: %.1f cm, Water Level: %.1f%%\n", 
                currentDistance, currentWaterLevel);
  
  // Set initial LED state based on configuration
  if (!config.ledEnabled) {
    digitalWrite(LED_PIN, HIGH); // Turn off LED (HIGH = off for built-in LED)
    Serial.println("LED disabled in configuration");
  } else {
    Serial.println("LED enabled in configuration - will blink every 3 seconds");
  }
  
  Serial.println("Configuration mode started");
  Serial.println("Look for WiFi network with prefix: WATER_SENSOR_");
}

void loop() {
  server.handleClient();
  
  // Update sensor readings based on refresh rate
  updateSensorReadings();
  
  // Handle ESP-NOW retries for failed sends
  if (espNowInitialized && !espNowSendSuccess && (millis() - lastEspNowRetry >= ESP_NOW_RETRY_MS)) {
    Serial.println("=== ESP-NOW RETRY ATTEMPT ===");
    Serial.printf("Retrying failed send to %s\n", macToString(config.parentMac.data()).c_str());
    sendEspNowData();
    lastEspNowRetry = millis();
  }
  
  // Blink LED to indicate device is working (only if enabled)
  static uint32_t lastBlink = 0;
  static bool ledState = false;
  if(config.ledEnabled && millis() - lastBlink > 3000) { // Blink every 3 seconds
    ledState = !ledState; // Toggle LED state
    digitalWrite(LED_PIN, ledState ? LOW : HIGH); // LOW = on, HIGH = off
    lastBlink = millis();
  }
  
  checkButton();
}
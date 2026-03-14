#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>

// ========= YOUR SETTINGS =========
const char* ssid = "Siddharths Galaxy M42 5G";
const char* pass = "88888889";
const char* apiKey = "";
const char* mdnsName = "garden";
// =================================

// Pins
#define SENSOR1 34
#define SENSOR2 35
#define SENSOR3 32
#define SENSOR4 33
#define RELAY_PUMP 25
#define RELAY_FAN 26
#define RELAY_LIGHT 27
#define DHT_PIN 4
#define LDR_PIN 39

// DHT Sensor type
#define DHT_TYPE DHT22

// Active LOW relay
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// Thresholds
int dryThreshold = 1700;
int wetStop = 1000;
float tempThreshold = 30.0;
float humThreshold = 70.0;
int darkThreshold = 1000;
int lightThreshold = 500;

// Plant Profiles
String plantTypes[4] = {"Tomato", "Basil", "Aloe Vera", "Custom"};
int plantDryThreshold[4] = {1700, 1600, 2000, 1700};
int plantWetStop[4] = {1000, 900, 1200, 1000};

// State
bool autoMode = true;
bool manualPump = false;
bool manualFan = false;
bool manualLight = false;
bool pumpState = false;
bool fanState = false;
bool lightState = false;

// 5s confirmation for AUTO mode only
bool pendingPumpState = false;
bool pendingFanState = false;
bool pendingLightState = false;
unsigned long changeTimePump = 0;
unsigned long changeTimeFan = 0;
unsigned long changeTimeLight = 0;
const unsigned long confirmDelay = 5000;

// Track if changes are from manual control
bool manualPumpChange = false;
bool manualFanChange = false;
bool manualLightChange = false;

// DHT Sensor
DHT dht(DHT_PIN, DHT_TYPE);

// Server
WebServer server(80);

// --------- Helpers ----------
bool keyOk() {
  if (apiKey[0] == '\0') return true;
  if (!server.hasArg("api_key")) return false;
  return (server.arg("api_key") == apiKey);
}

// Set CORS headers
void setCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Moisture to percentage conversion
int moistureToPercentage(int rawValue, int dryThresh, int wetThresh) {
  // Map: dryThreshold → 0%, wetStop → 100%
  int percentage = map(rawValue, dryThresh, wetThresh, 0, 100);
  // Constrain to 0-100%
  return constrain(percentage, 0, 100);
}

void setPump(bool on) {
  pumpState = on;
  digitalWrite(RELAY_PUMP, pumpState ? RELAY_ON : RELAY_OFF);
  Serial.println("Pump " + String(pumpState ? "ON" : "OFF"));
}

void setFan(bool on) {
  fanState = on;
  digitalWrite(RELAY_FAN, fanState ? RELAY_ON : RELAY_OFF);
  Serial.println("Fan " + String(fanState ? "ON" : "OFF"));
}

void setLight(bool on) {
  lightState = on;
  digitalWrite(RELAY_LIGHT, lightState ? RELAY_ON : RELAY_OFF);
  Serial.println("Light " + String(lightState ? "ON" : "OFF"));
}

String jsonStatus(int m1, int m2, int m3, int m4, float temperature, float humidity, int ldrValue) {
  String s = "{";
  
  // Moisture values as percentages
  s += "\"s1\":" + String(moistureToPercentage(m1, plantDryThreshold[0], plantWetStop[0])) + ",";
  s += "\"s2\":" + String(moistureToPercentage(m2, plantDryThreshold[1], plantWetStop[1])) + ",";
  s += "\"s3\":" + String(moistureToPercentage(m3, plantDryThreshold[2], plantWetStop[2])) + ",";
  s += "\"s4\":" + String(moistureToPercentage(m4, plantDryThreshold[3], plantWetStop[3])) + ",";
  
  // Raw sensor values (optional - for debugging)
  s += "\"raw1\":" + String(m1) + ",";
  s += "\"raw2\":" + String(m2) + ",";
  s += "\"raw3\":" + String(m3) + ",";
  s += "\"raw4\":" + String(m4) + ",";
  
  s += "\"temperature\":" + String(temperature, 1) + ",";
  s += "\"humidity\":" + String(humidity, 1) + ",";
  s += "\"ldr\":" + String(ldrValue) + ",";
  s += "\"pump\":" + String(pumpState ? 1 : 0) + ",";
  s += "\"fan\":" + String(fanState ? 1 : 0) + ",";
  s += "\"light\":" + String(lightState ? 1 : 0) + ",";
  s += "\"auto\":" + String(autoMode ? 1 : 0) + ",";
  s += "\"manualPump\":" + String(manualPump ? 1 : 0) + ",";
  s += "\"manualFan\":" + String(manualFan ? 1 : 0) + ",";
  s += "\"manualLight\":" + String(manualLight ? 1 : 0) + ",";
  
  // Plant types
  s += "\"plant1Type\":\"" + plantTypes[0] + "\",";
  s += "\"plant2Type\":\"" + plantTypes[1] + "\",";
  s += "\"plant3Type\":\"" + plantTypes[2] + "\",";
  s += "\"plant4Type\":\"" + plantTypes[3] + "\",";
  
  // Thresholds
  s += "\"dryThreshold\":" + String(dryThreshold) + ",";
  s += "\"wetStop\":" + String(wetStop) + ",";
  s += "\"tempThreshold\":" + String(tempThreshold, 1) + ",";
  s += "\"humThreshold\":" + String(humThreshold, 1) + ",";
  s += "\"darkThreshold\":" + String(darkThreshold) + ",";
  s += "\"lightThreshold\":" + String(lightThreshold);
  s += "}";
  return s;
}

// --------- HTTP Handlers ----------
void handleStatus() {
  setCORSHeaders();
  
  if (!keyOk()) { 
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}"); 
    return;
  }
  
  int m1 = analogRead(SENSOR1);
  int m2 = analogRead(SENSOR2);
  int m3 = analogRead(SENSOR3);
  int m4 = analogRead(SENSOR4);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int ldrValue = analogRead(LDR_PIN);
  
  if (isnan(temperature) || isnan(humidity)) {
    temperature = -1;
    humidity = -1;
  }
  
  server.send(200, "application/json", jsonStatus(m1, m2, m3, m4, temperature, humidity, ldrValue));
}

void handleMode() {
  setCORSHeaders();
  
  if (!keyOk()) { 
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}"); 
    return; 
  }
  
  // Toggle mode instead of requiring a parameter
  autoMode = !autoMode;
  
  // Reset manual states when switching to auto mode
  if (autoMode) {
    manualPump = false;
    manualFan = false;
    manualLight = false;
    
    // Reset pending states for auto mode
    pendingPumpState = pumpState;
    pendingFanState = fanState;
    pendingLightState = lightState;
    changeTimePump = millis();
    changeTimeFan = millis();
    changeTimeLight = millis();
  }
  
  server.send(200, "application/json", String("{\"ok\":true,\"auto\":") + (autoMode?1:0) + "}");
}

void handlePump() {
  setCORSHeaders();
  
  if (!keyOk()) {
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  if (autoMode) { 
    server.send(409, "application/json", "{\"error\":\"auto mode active\"}"); 
    return; 
  }
  
  // Toggle pump state - apply immediately in manual mode
  manualPump = !manualPump;
  setPump(manualPump);  // Apply immediately for manual control
  
  // Mark as manual change and update timers
  manualPumpChange = true;
  pendingPumpState = manualPump;
  changeTimePump = millis();
  
  server.send(200, "application/json", String("{\"ok\":true,\"manualPump\":") + (manualPump?1:0) + "}");
}

void handleFan() {
  setCORSHeaders();
  
  if (!keyOk()) {
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  if (autoMode) { 
    server.send(409, "application/json", "{\"error\":\"auto mode active\"}"); 
    return; 
  }
  
  // Toggle fan state - apply immediately in manual mode
  manualFan = !manualFan;
  setFan(manualFan);  // Apply immediately for manual control
  
  // Mark as manual change and update timers
  manualFanChange = true;
  pendingFanState = manualFan;
  changeTimeFan = millis();
  
  server.send(200, "application/json", String("{\"ok\":true,\"manualFan\":") + (manualFan?1:0) + "}");
}

void handleLight() {
  setCORSHeaders();
  
  if (!keyOk()) {
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  if (autoMode) { 
    server.send(409, "application/json", "{\"error\":\"auto mode active\"}"); 
    return; 
  }
  
  // Toggle light state - apply immediately in manual mode
  manualLight = !manualLight;
  setLight(manualLight);  // Apply immediately for manual control
  
  // Mark as manual change and update timers
  manualLightChange = true;
  pendingLightState = manualLight;
  changeTimeLight = millis();
  
  server.send(200, "application/json", String("{\"ok\":true,\"manualLight\":") + (manualLight?1:0) + "}");
}

void handleThresholds() {
  setCORSHeaders();
  
  if (!keyOk()) { 
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  if (server.hasArg("dry")) dryThreshold = server.arg("dry").toInt();
  if (server.hasArg("wet")) wetStop = server.arg("wet").toInt();
  if (server.hasArg("temp")) tempThreshold = server.arg("temp").toFloat();
  if (server.hasArg("hum")) humThreshold = server.arg("hum").toFloat();
  if (server.hasArg("dark")) darkThreshold = server.arg("dark").toInt();
  if (server.hasArg("light")) lightThreshold = server.arg("light").toInt();
  
  server.send(200, "application/json", 
    String("{\"ok\":true,\"dryThreshold\":") + dryThreshold + 
    ",\"wetStop\":" + wetStop +
    ",\"tempThreshold\":" + String(tempThreshold, 1) +
    ",\"humThreshold\":" + String(humThreshold, 1) +
    ",\"darkThreshold\":" + darkThreshold +
    ",\"lightThreshold\":" + lightThreshold + "}");
}

// New endpoint to update plant profiles
void handlePlantProfile() {
  setCORSHeaders();
  
  if (!keyOk()) { 
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  
  if (server.hasArg("plant1")) plantTypes[0] = server.arg("plant1");
  if (server.hasArg("plant2")) plantTypes[1] = server.arg("plant2");
  if (server.hasArg("plant3")) plantTypes[2] = server.arg("plant3");
  if (server.hasArg("plant4")) plantTypes[3] = server.arg("plant4");
  
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Plant profiles updated\"}");
}

// New endpoint to update plant thresholds
void handlePlantConfig() {
  setCORSHeaders();
  
  if (!keyOk()) { 
    server.send(401, "application/json", "{\"error\":\"bad api_key\"}");
    return; 
  }
  
  if (server.hasArg("plantIndex") && server.hasArg("dryThresh") && server.hasArg("wetThresh")) {
    int index = server.arg("plantIndex").toInt();
    if (index >= 0 && index < 4) {
      plantDryThreshold[index] = server.arg("dryThresh").toInt();
      plantWetStop[index] = server.arg("wetThresh").toInt();
      
      Serial.println("Updated plant " + String(index+1) + " thresholds:");
      Serial.println("  Dry: " + String(plantDryThreshold[index]));
      Serial.println("  Wet: " + String(plantWetStop[index]));
      
      server.send(200, "application/json", "{\"ok\":true,\"message\":\"Plant thresholds updated\"}");
      return;
    }
  }
  
  server.send(400, "application/json", "{\"error\":\"bad parameters\"}");
}

void handleOptions() {
  setCORSHeaders();
  server.send(200);
}

// --------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  
  dht.begin();
  
  // Test all relays on startup
  Serial.println("Testing relays...");
  setPump(true); delay(1000); setPump(false);
  setFan(true); delay(1000); setFan(false);
  setLight(true); delay(1000); setLight(false);
  Serial.println("Relay test complete");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("WiFi: ");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected.");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  if (MDNS.begin(mdnsName)) {
    Serial.print("mDNS: http://"); Serial.print(mdnsName); Serial.println(".local");
  }

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/pump", HTTP_POST, handlePump);
  server.on("/fan", HTTP_POST, handleFan);
  server.on("/light", HTTP_POST, handleLight);
  server.on("/thresholds", HTTP_POST, handleThresholds);
  server.on("/plantprofile", HTTP_POST, handlePlantProfile);
  server.on("/plantconfig", HTTP_POST, handlePlantConfig);
  
  // Add OPTIONS handlers for CORS preflight requests
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/mode", HTTP_OPTIONS, handleOptions);
  server.on("/pump", HTTP_OPTIONS, handleOptions);
  server.on("/fan", HTTP_OPTIONS, handleOptions);
  server.on("/light", HTTP_OPTIONS, handleOptions);
  server.on("/thresholds", HTTP_OPTIONS, handleOptions);
  server.on("/plantprofile", HTTP_OPTIONS, handleOptions);
  server.on("/plantconfig", HTTP_OPTIONS, handleOptions);

  server.on("/", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "text/html", "ESP32 Vertical Garden Server is Running.<br><br> Try <a href=\"/status\">/status</a>");
  });

  server.begin();
  Serial.println("HTTP server started.");
  
  // Print plant profiles
  Serial.println("Plant Profiles Loaded:");
  for (int i = 0; i < 4; i++) {
    Serial.println("Plant " + String(i+1) + ": " + plantTypes[i] + 
                   " (Dry: " + String(plantDryThreshold[i]) + 
                   ", Wet: " + String(plantWetStop[i]) + ")");
  }
}

void loop() {
  server.handleClient();

  int m1 = analogRead(SENSOR1);
  int m2 = analogRead(SENSOR2);
  int m3 = analogRead(SENSOR3);
  int m4 = analogRead(SENSOR4);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int ldrValue = analogRead(LDR_PIN);
  
  if (isnan(temperature) || isnan(humidity)) {
    temperature = -1;
    humidity = -1;
  }

  bool desiredPump = pumpState;
  bool desiredFan = fanState;
  bool desiredLight = lightState;

  if (autoMode) {
    // AUTO MODE: Use sensor-based logic with 5s confirmation delay
    
    // Pump logic - check all plants
    bool needPump = false;
    int sensors[4] = {m1, m2, m3, m4};
    
    for (int i = 0; i < 4; i++) {
      // If any plant is too dry, we need to water
      if (sensors[i] > plantDryThreshold[i]) {
        needPump = true;
        Serial.println("Plant " + String(i+1) + " needs water: " + String(sensors[i]) + " > " + String(plantDryThreshold[i]));
      }
      // If any plant is too wet, we should stop watering
      if (sensors[i] < plantWetStop[i]) {
        needPump = false;
        Serial.println("Plant " + String(i+1) + " is too wet: " + String(sensors[i]) + " < " + String(plantWetStop[i]));
        break; // Stop watering if any plant is too wet
      }
    }
    desiredPump = needPump;

    // Fan logic
    bool needFan = false;
    if (temperature > tempThreshold) {
      needFan = true;
      Serial.println("Temperature too high: " + String(temperature) + " > " + String(tempThreshold));
    }
    if (humidity > humThreshold) {
      needFan = true;
      Serial.println("Humidity too high: " + String(humidity) + " > " + String(humThreshold));
    }
    desiredFan = needFan;

    // Light logic
    bool needLight = false;
    if (ldrValue > darkThreshold) {
      needLight = true;
      Serial.println("Too dark: " + String(ldrValue) + " > " + String(darkThreshold));
    } else if (ldrValue < lightThreshold) {
      needLight = false;
      Serial.println("Enough light: " + String(ldrValue) + " < " + String(lightThreshold));
    }
    desiredLight = needLight;
    
  } else {
    // MANUAL MODE: Use manual control states
    desiredPump = manualPump;
    desiredFan = manualFan;
    desiredLight = manualLight;
  }

  // Apply changes with appropriate timing
  if (!autoMode) {
    // MANUAL MODE: Apply changes immediately
    if (desiredPump != pumpState) {
      setPump(desiredPump);
    }
    if (desiredFan != fanState) {
      setFan(desiredFan);
    }
    if (desiredLight != lightState) {
      setLight(desiredLight);
    }
  } else {
    // AUTO MODE: Use 5-second confirmation delay to avoid sensor glitches
    
    // Pump confirmation
    if (desiredPump != pumpState) {
      if (desiredPump != pendingPumpState || manualPumpChange) {
        pendingPumpState = desiredPump;
        changeTimePump = millis();
        manualPumpChange = false;
        Serial.println("Pump state change requested, waiting 5s");
      } else if (millis() - changeTimePump >= confirmDelay) {
        setPump(pendingPumpState);
      }
    } else {
      pendingPumpState = pumpState;
      manualPumpChange = false;
    }

    // Fan confirmation
    if (desiredFan != fanState) {
      if (desiredFan != pendingFanState || manualFanChange) {
        pendingFanState = desiredFan;
        changeTimeFan = millis();
        manualFanChange = false;
        Serial.println("Fan state change requested, waiting 5s");
      } else if (millis() - changeTimeFan >= confirmDelay) {
        setFan(pendingFanState);
      }
    } else {
      pendingFanState = fanState;
      manualFanChange = false;
    }

    // Light confirmation
    if (desiredLight != lightState) {
      if (desiredLight != pendingLightState || manualLightChange) {
        pendingLightState = desiredLight;
        changeTimeLight = millis();
        manualLightChange = false;
        Serial.println("Light state change requested, waiting 5s");
      } else if (millis() - changeTimeLight >= confirmDelay) {
        setLight(pendingLightState);
      }
    } else {
      pendingLightState = lightState;
      manualLightChange = false;
    }
  }

  delay(300);
}

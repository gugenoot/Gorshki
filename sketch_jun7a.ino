#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// сеть, где находится устройство)
const char* ssid = "WiFi_где_стоит_горшок";
const char* password = "Пароль_этой_сети";

const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

// ID вашего устройства (из базы данных)
const char* device_id = "404f8fec-472d-4215-ab1d-a1de8d4a96ae";

const int soilSensorPin = A0;
const int waterLevelPin = D1;
const int pumpPin = D2;
const int dhtPin = D3;

#define DHTTYPE DHT11
DHT dht(dhtPin, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

const int pumpDuration = 10000;
unsigned long pumpStartTime = 0;
bool pumpState = false;

float readSoilMoisture() {
  int value = analogRead(soilSensorPin);
  float percent = map(value, 0, 1023, 100, 0);
  return constrain(percent, 0, 100);
}

float readTemperature() {
  float temp = dht.readTemperature();
  if (isnan(temp)) return 22.5;
  return temp;
}

float readAirHumidity() {
  float hum = dht.readHumidity();
  if (isnan(hum)) return 55.0;
  return hum;
}

bool readWaterLevel() {
  return digitalRead(waterLevelPin) == LOW;
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  Serial.print("Command received: ");
  Serial.println(message);
  
  if (String(topic).endsWith("/water")) {
    digitalWrite(pumpPin, HIGH);
    pumpState = true;
    pumpStartTime = millis();
    Serial.println("💧 Watering started!");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker...");
    String client_id = "smartpot_" + String(device_id);
    if (client.connect(client_id.c_str())) {
      Serial.println("connected!");
      String commandTopic = String("command/") + device_id + "/#";
      client.subscribe(commandTopic.c_str());
      Serial.print("Subscribed to: ");
      Serial.println(commandTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 sec");
      delay(5000);
    }
  }
}

void sendTelemetry() {
  float soil = readSoilMoisture();
  float temp = readTemperature();
  float airHum = readAirHumidity();
  bool waterLevel = readWaterLevel();
  
  StaticJsonDocument<200> doc;
  doc["soilHumidity"] = soil;
  doc["temperature"] = temp;
  doc["airHumidity"] = airHum;
  doc["waterLevel"] = waterLevel;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  String topic = String("telemetry/") + device_id;
  if (client.publish(topic.c_str(), jsonString.c_str())) {
    Serial.print("📤 Telemetry sent: ");
    Serial.println(jsonString);
  } else {
    Serial.println("Failed to send!");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  pinMode(waterLevelPin, INPUT_PULLUP);
  dht.begin();
  
  Serial.println("\n=== Smart Pot Starting ===");
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  
  if (pumpState && (millis() - pumpStartTime >= pumpDuration)) {
    digitalWrite(pumpPin, LOW);
    pumpState = false;
    Serial.println("⏹Watering stopped");
  }
  
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 30000) {
    sendTelemetry();
    lastSend = millis();
  }
}
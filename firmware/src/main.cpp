#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "secrets.h"  // WIFI_SSID, WIFI_PASS, TOPIC_PREFIX (see secrets.h.example)

// =====================================================
// MQTT
// =====================================================
const char* MQTT_SERVER = "broker.hivemq.com";  // public broker: use a unique TOPIC_PREFIX
const int MQTT_PORT = 1883;

const String MQTT_EVENTS_TOPIC = String(TOPIC_PREFIX) + "/events";
const String MQTT_COMMANDS_TOPIC = String(TOPIC_PREFIX) + "/commands";
const char* DEVICE_ID = "sentinel_01";

// =====================================================
// Hardware pins
// =====================================================
const int PIR_PIN = 4;             // HC-SR501 OUT
const int LED_PIN = 33;            // LED
const int BUZZER_PIN = 26;         // Buzzer
const int BUTTON_PIN = 35;         // Manual OFF button (input-only pin, external pull-down)
const int TRIGGER_BUTTON_PIN = 34; // Manual detection trigger (input-only pin, external pull-down)

// =====================================================
// Motion configuration
// =====================================================
const unsigned long MOTION_CONFIRM_TIME = 5000;  // motion must last 5 s
const unsigned long MOTION_COOLDOWN = 10000;     // min 10 s between events

// =====================================================
// Timezone (Morocco)
// =====================================================
const char* TZ_INFO = "Africa/Casablanca";

// =====================================================
// Objects and state
// =====================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool motionActive = false;
bool eventSent = false;
bool alarmActive = false;

unsigned long motionStartTime = 0;
unsigned long lastMotionEvent = 0;

bool lastButtonState = LOW;
bool lastTriggerButtonState = LOW;

// =====================================================
// Time helpers
// =====================================================
bool getDateTime(String &date, String &time)
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain current time");
    return false;
  }

  char dateBuffer[11];
  char timeBuffer[9];

  strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);

  date = String(dateBuffer);
  time = String(timeBuffer);

  return true;
}

void setupTime()
{
  Serial.println();
  Serial.println("Synchronizing time...");

  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int attempts = 0;

  while (!getLocalTime(&timeinfo) && attempts < 20)
  {
    Serial.print(".");
    delay(500);
    attempts++;
  }

  Serial.println();

  if (getLocalTime(&timeinfo))
  {
    String date;
    String time;
    getDateTime(date, time);

    Serial.println("Time synchronized");
    Serial.println("Date: " + date);
    Serial.println("Time: " + time);
  }
  else
  {
    Serial.println("Could not synchronize time");
  }
}

// =====================================================
// WiFi
// =====================================================
void setupWiFi()
{
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
}

// =====================================================
// Publish motion event
// =====================================================
void publishMotionEvent()
{
  String currentDate;
  String currentTime;

  if (!getDateTime(currentDate, currentTime))
  {
    currentDate = "UNKNOWN";
    currentTime = "UNKNOWN";
  }

  JsonDocument doc;

  doc["event"] = "MOTION_DETECTED";
  doc["device_id"] = DEVICE_ID;
  doc["motion"] = true;
  doc["date"] = currentDate;
  doc["time"] = currentTime;

  char buffer[256];
  serializeJson(doc, buffer);

  Serial.println();
  Serial.println("---------- MQTT PUBLISH ----------");
  Serial.println("Topic: " + MQTT_EVENTS_TOPIC);
  Serial.print("Payload: ");
  Serial.println(buffer);

  if (mqttClient.publish(MQTT_EVENTS_TOPIC.c_str(), buffer))
  {
    Serial.println("Motion event published");
  }
  else
  {
    Serial.println("MQTT publish failed");
  }
}

// =====================================================
// Alarm
// =====================================================
void alarmOn()
{
  alarmActive = true;

  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);

  Serial.println("ALARM ON (LED + buzzer)");
}

void alarmOff()
{
  alarmActive = false;

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("ALARM OFF");
}

// =====================================================
// MQTT callback (commands from the workflow)
// =====================================================
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  Serial.println();
  Serial.println("---------- MQTT COMMAND ----------");
  Serial.print("Topic: ");
  Serial.println(topic);

  String message;

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.print("Payload: ");
  Serial.println(message);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error)
  {
    Serial.print("JSON error: ");
    Serial.println(error.c_str());
    return;
  }

  const char* command = doc["command"];

  if (command == nullptr)
  {
    Serial.println("No command received");
    return;
  }

  if (strcmp(command, "ALARM_ON") == 0)
  {
    alarmOn();
  }
  else if (strcmp(command, "ALARM_OFF") == 0)
  {
    alarmOff();
  }
  else if (strcmp(command, "LED_ON") == 0)
  {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
  }
  else if (strcmp(command, "LED_OFF") == 0)
  {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
  }
  else
  {
    Serial.print("Unknown command: ");
    Serial.println(command);
  }
}

// =====================================================
// MQTT reconnect
// =====================================================
void reconnectMQTT()
{
  while (!mqttClient.connected())
  {
    Serial.print("Connecting to MQTT... ");

    String clientId = "sentinel_esp32_";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqttClient.connect(clientId.c_str()))
    {
      Serial.println("connected");

      if (mqttClient.subscribe(MQTT_COMMANDS_TOPIC.c_str()))
      {
        Serial.println("Subscribed to: " + MQTT_COMMANDS_TOPIC);
      }
      else
      {
        Serial.println("Subscribe failed");
      }
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(3000);
    }
  }
}

// =====================================================
// Setup
// =====================================================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(TRIGGER_BUTTON_PIN, INPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println();
  Serial.println("==================================");
  Serial.println("SENTINEL SYSTEM");
  Serial.println("==================================");

  setupWiFi();
  setupTime();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println();
  Serial.println("System ready.");
}

// =====================================================
// Main loop
// =====================================================
void loop()
{
  if (!mqttClient.connected())
  {
    reconnectMQTT();
  }

  mqttClient.loop();

  // ---------- HC-SR501 motion detection ----------
  bool pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH && !motionActive)
  {
    motionActive = true;
    eventSent = false;
    motionStartTime = millis();

    Serial.println();
    Serial.println("Motion detected, starting 5-second confirmation window");
  }

  if (motionActive && !eventSent)
  {
    unsigned long elapsed = millis() - motionStartTime;

    if (elapsed >= MOTION_CONFIRM_TIME && millis() - lastMotionEvent >= MOTION_COOLDOWN)
    {
      publishMotionEvent();

      lastMotionEvent = millis();
      eventSent = true;

      Serial.println("Motion event sent to the workflow");
    }
  }

  if (pirState == LOW && motionActive)
  {
    motionActive = false;
    Serial.println("Motion cleared");
  }

  // ---------- Manual OFF button (GPIO 35) ----------
  bool buttonState = digitalRead(BUTTON_PIN);

  if (buttonState == HIGH && lastButtonState == LOW)
  {
    Serial.println();
    Serial.println("Manual OFF button pressed");
    alarmOff();
    delay(150);
  }

  lastButtonState = buttonState;

  // ---------- Manual detection trigger button (GPIO 34) ----------
  bool triggerButtonState = digitalRead(TRIGGER_BUTTON_PIN);

  if (triggerButtonState == HIGH && lastTriggerButtonState == LOW)
  {
    Serial.println();
    Serial.println("Manual detection button pressed, sending MOTION_DETECTED...");

    publishMotionEvent();
    lastMotionEvent = millis();

    delay(150);
  }

  lastTriggerButtonState = triggerButtonState;
}

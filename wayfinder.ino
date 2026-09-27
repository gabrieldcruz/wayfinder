#include <WiFi.h>

// Arduino Nano ESP32 pin labels
const int trigPin = D12;
const int echoPin = D11;
const int light = D5;
const int button = D9;
const int speaker = D10;

// Reserved for future Wi-Fi features messaging emerency contacts 
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

const float URGENT_DISTANCE_CM = 100.0; 
const float SILENT_DISTANCE_CM = 400.0;

const unsigned long SENSOR_INTERVAL_MS = 80;
const unsigned long BEEP_DURATION_MS = 50;

const int NORMAL_FREQUENCY_HZ = 1500;
const int URGENT_FREQUENCY_HZ = 2500;

unsigned long lastMeasurement = 0;
unsigned long lastBeepStart = 0;

float distance = 0;
bool validReading = false;
bool beepOn = false;
bool alertActive = false;
bool urgentActive = false;

void setup() {
  Serial.begin(9600);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(light, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  pinMode(speaker, OUTPUT);

  digitalWrite(trigPin, LOW);
  digitalWrite(light, LOW);
  digitalWrite(speaker, LOW);
}

void loop() {
  // Read the ultrasonic sensor every 80 ms.
  if (millis() - lastMeasurement >= SENSOR_INTERVAL_MS) {
    lastMeasurement = millis();

    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
    validReading = (duration > 0);

    if (validReading) {
      distance = duration * 0.0343f / 2.0f;

      Serial.print("Distance: ");
      Serial.print(distance);
      Serial.println(" cm");
    } else {
      Serial.println("No echo");
    }
  }

  unsigned long now = millis();

  // No echo or 4 meters and beyond: silence.
  if (!validReading || distance >= SILENT_DISTANCE_CM) {
    if (beepOn) {
      noTone(speaker);
    }

    beepOn = false;
    alertActive = false;
    urgentActive = false;
    return;
  }

  // Within 1 meter: continuous high-pitched alarm.
  if (distance <= URGENT_DISTANCE_CM) {
    if (!urgentActive) {
      tone(speaker, URGENT_FREQUENCY_HZ);
    }

    beepOn = true;
    urgentActive = true;
    alertActive = false;
    return;
  }

  // Object moved beyond 1 meter: switch back to normal beeps.
  if (urgentActive) {
    noTone(speaker);
    beepOn = false;
    urgentActive = false;
    alertActive = false;
  }

  // Stop each normal beep after 50 ms.
  if (beepOn && now - lastBeepStart >= BEEP_DURATION_MS) {
    noTone(speaker);
    beepOn = false;
  }

  // Just beyond 1 meter: beep every 120 ms.
  // Approaching 4 meters: beep every 1500 ms.
  float limitedDistance = constrain(
    distance, URGENT_DISTANCE_CM, SILENT_DISTANCE_CM
  );

  unsigned long beepInterval = (unsigned long)(
    120.0f +
    (limitedDistance - URGENT_DISTANCE_CM) *
    (1500.0f - 120.0f) /
    (SILENT_DISTANCE_CM - URGENT_DISTANCE_CM)
  );

  if (!alertActive || now - lastBeepStart >= beepInterval) {
    tone(speaker, NORMAL_FREQUENCY_HZ);

    lastBeepStart = now;
    beepOn = true;
    alertActive = true;
  }
}
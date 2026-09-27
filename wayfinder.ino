#include <WiFi.h>

const char* ssid = "iGabriel ";
const char* password = "Pa23word";

// ---------- Pins ----------
const int switchPin = D8;
const int trigPin = D12;
const int echoPin = D11;
const int light = D5;
const int button = D9;
const int speaker = D10;

// ---------- Settings ----------
const int BEEP_FREQUENCY_HZ = 1500;

const float URGENT_DISTANCE_CM = 100.0;
const float SILENT_DISTANCE_CM = 400.0;

const unsigned long SENSOR_INTERVAL_MS = 80;
const unsigned long BEEP_DURATION_MS = 50;
const unsigned long HOLD_TIME_MS = 5000;
const unsigned long DEBOUNCE_MS = 30;

// ---------- Switch ----------
bool systemEnabled = false;
int lastSwitchReading = HIGH;
unsigned long switchChangedAt = 0;

// ---------- Help button ----------
int lastButtonReading = HIGH;
int stableButtonState = HIGH;

unsigned long buttonChangedAt = 0;
unsigned long buttonPressedAt = 0;

bool holdDetected = false;

// ---------- Sensor and sound ----------
unsigned long lastMeasurement = 0;
unsigned long lastBeepStart = 0;

float distance = 0;
bool validReading = false;
bool beepOn = false;
bool alertActive = false;

// ---------- Wi-Fi ----------
bool wifiWasConnected = false;
unsigned long lastWiFiRetry = 0;
unsigned long lastWiFiPrint = 0;

void setSystemEnabled(bool enabled) {
  systemEnabled = enabled;

  // Clear old readings, sounds, and button-hold timing.
  noTone(speaker);
  digitalWrite(speaker, LOW);
  digitalWrite(light, LOW);
  digitalWrite(trigPin, LOW);

  validReading = false;
  beepOn = false;
  alertActive = false;
  distance = 0;

  lastButtonReading = HIGH;
  stableButtonState = HIGH;
  buttonChangedAt = millis();
  buttonPressedAt = 0;
  holdDetected = false;

  wifiWasConnected = false;

  if (enabled) {
    Serial.println("\nSYSTEM ON");

    lastMeasurement = millis();
    lastWiFiRetry = millis();
    lastWiFiPrint = millis();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    Serial.println("Connecting to WiFi...");
    Serial.println("Hold help button for 5 seconds to test alert.");
  } else {
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);

    Serial.println("\nSYSTEM OFF — STANDBY");
    Serial.println("Sensor, sound, button test and WiFi stopped.");
  }
}

void updateSwitch() {
  unsigned long now = millis();
  int reading = digitalRead(switchPin);

  if (reading != lastSwitchReading) {
    lastSwitchReading = reading;
    switchChangedAt = now;
  }

  if (now - switchChangedAt >= DEBOUNCE_MS) {
    bool requestedEnabled = (reading == LOW);

    if (requestedEnabled != systemEnabled) {
      setSystemEnabled(requestedEnabled);
    }
  }
}

void updateButton() {
  unsigned long now = millis();
  int reading = digitalRead(button);

  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    buttonChangedAt = now;
  }

  if (now - buttonChangedAt >= DEBOUNCE_MS &&
      reading != stableButtonState) {
    stableButtonState = reading;

    if (stableButtonState == LOW) {
      buttonPressedAt = now;
      holdDetected = false;

      digitalWrite(light, HIGH);
      Serial.println("BUTTON PRESSED");
    } else {
      digitalWrite(light, LOW);
      Serial.println("BUTTON RELEASED");

      if (!holdDetected) {
        Serial.println("Released before 5 seconds — no alert.");
      }

      holdDetected = false;
    }
  }

  if (stableButtonState == LOW &&
      !holdDetected &&
      now - buttonPressedAt >= HOLD_TIME_MS) {
    holdDetected = true;

    Serial.println("5-SECOND HOLD DETECTED!");
    Serial.println("An emergency message would be sent here.");
    // Test only: no actual message is sent yet.
  }
}

void updateWiFi() {
  unsigned long now = millis();
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !wifiWasConnected) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else if (!connected && wifiWasConnected) {
    Serial.println("WiFi disconnected.");
  }

  wifiWasConnected = connected;

  if (!connected && now - lastWiFiPrint >= 5000) {
    lastWiFiPrint = now;
    Serial.print("WiFi status: ");
    Serial.println((int)WiFi.status());
  }

  if (!connected && now - lastWiFiRetry >= 20000) {
    lastWiFiRetry = now;
    Serial.println("Retrying WiFi...");
    WiFi.reconnect();
  }
}

void updateObstacleAlert() {
  if (beepOn &&
      millis() - lastBeepStart >= BEEP_DURATION_MS) {
    noTone(speaker);
    beepOn = false;
  }

  if (millis() - lastMeasurement >= SENSOR_INTERVAL_MS) {
    lastMeasurement = millis();

    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    unsigned long duration =
        pulseIn(echoPin, HIGH, 30000UL);

    validReading = (duration > 0);

    if (validReading) {
      distance = duration * 0.0343f / 2.0f;

      // Limit logging so button messages remain readable.
      static unsigned long lastDistancePrint = 0;

      if (millis() - lastDistancePrint >= 500) {
        lastDistancePrint = millis();
        Serial.print("Distance: ");
        Serial.print(distance);
        Serial.println(" cm");
      }
    }
  }

  unsigned long now = millis();

  if (!validReading || distance >= SILENT_DISTANCE_CM) {
    if (beepOn) {
      noTone(speaker);
    }

    beepOn = false;
    alertActive = false;
    return;
  }

  if (beepOn && now - lastBeepStart >= BEEP_DURATION_MS) {
    noTone(speaker);
    beepOn = false;
  }

  unsigned long beepInterval;

  if (distance <= URGENT_DISTANCE_CM) {
    beepInterval = 120;
  } else {
    beepInterval = (unsigned long)(
      120.0f +
      (distance - URGENT_DISTANCE_CM) *
      (1500.0f - 120.0f) /
      (SILENT_DISTANCE_CM - URGENT_DISTANCE_CM)
    );
  }

  if (!alertActive ||
      now - lastBeepStart >= beepInterval) {
    tone(speaker, BEEP_FREQUENCY_HZ);

    lastBeepStart = now;
    beepOn = true;
    alertActive = true;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(button, INPUT_PULLUP);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(light, OUTPUT);
  pinMode(speaker, OUTPUT);

  // Start safely in standby; loop reads the switch.
  setSystemEnabled(false);
}

void loop() {
  // Always check the switch, including while suspended.
  updateSwitch();

  if (!systemEnabled) {
    delay(5);
    return;
  }

  updateButton();
  updateWiFi();
  updateObstacleAlert();

  delay(1);
}
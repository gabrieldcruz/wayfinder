#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------- Connection settings ----------
const char* ssid = "HOTSPOT_NAME";
const char* password = "HOTSPOT_PASSWORD";

const char* BOT_TOKEN = "TELEGRAM_TOKEN";
const char* CHAT_ID = "GROUP_ID";

// ---------- Arduino Nano ESP32 pins ----------
const int switchPin = D8;
const int light = D5;
const int button = D9;  
const int speaker = D10;

// Sensor 1
const int trigPin = D12;
const int echoPin = D11;

// Sensor 2
const int trigPin2 = D5;
const int echoPin2 = D4;

// ---------- Settings ----------
const int BEEP_FREQUENCY_HZ = 1500;

// Sensor 1: rapid beeps within 100 cm, silent at 375 cm or more.
const float URGENT_DISTANCE_CM = 100.0f;
const float SILENT_DISTANCE_CM = 375.0f;

// Sensor 2: only contributes to alerts at 50 cm or closer.
const float SENSOR2_ALERT_DISTANCE_CM = 50.0f;

// Alternate sensors every 80 ms.
// Each sensor updates approximately every 160 ms.
const unsigned long SENSOR_INTERVAL_MS = 80;
const unsigned long READING_MAX_AGE_MS = 500;
const unsigned long BEEP_DURATION_MS = 50;

const unsigned long HOLD_TIME_MS = 5000;
const unsigned long DEBOUNCE_MS = 30;

// ---------- Sound switch ----------
bool soundEnabled = false;
int lastSwitchReading = HIGH;
unsigned long switchChangedAt = 0;

// ---------- Help button ----------
int lastButtonReading = HIGH;
int stableButtonState = HIGH;

unsigned long buttonChangedAt = 0;
unsigned long buttonPressedAt = 0;

bool holdDetected = false;

// ---------- Ultrasonic sensors ----------
float sensorDistance[2] = {0.0f, 0.0f};
bool sensorValid[2] = {false, false};
unsigned long sensorUpdatedAt[2] = {0, 0};

int nextSensor = 0;
unsigned long lastMeasurement = 0;

// Closest valid reading within its sensor's alert range
float distance = 0;
bool validReading = false;

// ---------- Sound ----------
unsigned long lastBeepStart = 0;
bool beepOn = false;
bool alertActive = false;

// ---------- Wi-Fi ----------
bool wifiWasConnected = false;
bool clockReadyPrinted = false;

int lastWiFiStatus = -1;

unsigned long lastWiFiRetry = 0;
unsigned long lastWiFiPrint = 0;

// ---------- Telegram ----------
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

TaskHandle_t sosTaskHandle = nullptr;
std::atomic<bool> sosBusy{false};

// ---------- Wi-Fi status descriptions ----------
const char* wifiStatusText(int status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "Waiting for connection to finish.";

    case WL_NO_SSID_AVAIL:
      return "Hotspot not found. Check hotspot name and availability.";

    case WL_SCAN_COMPLETED:
      return "Network scan finished.";

    case WL_CONNECTED:
      return "Connected to hotspot.";

    case WL_CONNECT_FAILED:
      return "Connection attempt failed. Check password and hotspot settings.";

    case WL_CONNECTION_LOST:
      return "Connection to hotspot was lost.";

    case WL_DISCONNECTED:
      return "Disconnected from hotspot. Automatic reconnect is enabled.";

    case WL_NO_SHIELD:
      return "WiFi interface unavailable.";

    default:
      return "WiFi state unavailable.";
  }
}

// ---------- Telegram ----------
bool readyForSOS() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
      "NOT SENT: WiFi disconnected. Reconnect, release, and hold again."
    );
    return false;
  }

  if (time(nullptr) < 1700000000) {
    Serial.println(
      "NOT SENT: Clock is syncing. Wait, release, and hold again."
    );
    return false;
  }

  if (String(BOT_TOKEN).startsWith("YOUR_")) {
    Serial.println("NOT SENT: Fill in your current full bot token.");
    return false;
  }

  return true;
}

void performSOS() {
  if (!readyForSOS()) {
    return;
  }

  Serial.println("Sending SOS to Family Group Chat...");

  bool accepted = bot.sendMessage(
    CHAT_ID,
    "SOS - WAYFINDER SMART CANE\n\n"
    "Gabriel held the help button for 5 seconds.\n"
    "Please call or contact Gabriel now.",
    ""
  );

  if (accepted) {
    Serial.println("SUCCESS: Telegram accepted the group message.");
  } else {
    Serial.println(
      "Delivery NOT confirmed. Check internet, token, "
      "group ID, and bot permissions."
    );
  }

  Serial.println("Release the button before sending another alert.");
}

// Run Telegram separately so sending does not pause sensing.
void sosWorker(void* parameter) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    performSOS();

    client.stop();
    sosBusy.store(false);
  }
}

void sendSOS() {
  // SOS works regardless of the sound switch.
  if (!readyForSOS()) {
    return;
  }

  if (sosTaskHandle == nullptr) {
    Serial.println(
      "NOT SENT: SOS sender could not start. Restart the board."
    );
    return;
  }

  bool expected = false;

  if (!sosBusy.compare_exchange_strong(expected, true)) {
    Serial.println(
      "NOT SENT: Previous request still running. "
      "Release and hold again after it finishes."
    );
    return;
  }

  xTaskNotifyGive(sosTaskHandle);
}

// ---------- Sound switch ----------
void setSoundEnabled(bool enabled) {
  soundEnabled = enabled;

  noTone(speaker);
  digitalWrite(speaker, LOW);

  beepOn = false;
  alertActive = false;

  if (enabled) {
    Serial.println("\nSOUND ON");
  } else {
    Serial.println("\nSOUND OFF");
  }

  Serial.println("WiFi, both sensors, and SOS remain active.");
}

void updateSwitch() {
  unsigned long now = millis();
  int reading = digitalRead(switchPin);

  if (reading != lastSwitchReading) {
    lastSwitchReading = reading;
    switchChangedAt = now;
  }

  if (now - switchChangedAt >= DEBOUNCE_MS) {
    bool requestedSound = (reading == LOW);

    if (requestedSound != soundEnabled) {
      setSoundEnabled(requestedSound);
    }
  }
}

// ---------- Five-second SOS button ----------
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
        Serial.println("Released before 5 seconds - no alert.");
      }

      holdDetected = false;
    }
  }

  if (stableButtonState == LOW &&
      reading == LOW &&
      !holdDetected &&
      now - buttonPressedAt >= HOLD_TIME_MS) {
    holdDetected = true;

    Serial.println("5-SECOND HOLD DETECTED!");
    sendSOS();

    // One attempt per hold. Release before trying again.
  }
}

// ---------- Persistent Wi-Fi ----------
void updateWiFi() {
  unsigned long now = millis();
  int status = (int)WiFi.status();

  bool connected = (status == WL_CONNECTED);

  // Print immediately when status changes.
  // Repeat every 2 seconds while disconnected.
  if (status != lastWiFiStatus ||
      (!connected && now - lastWiFiPrint >= 2000)) {
    lastWiFiStatus = status;
    lastWiFiPrint = now;

    Serial.print("WiFi: ");
    Serial.println(wifiStatusText(status));
  }

  if (connected && !wifiWasConnected) {
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }

  wifiWasConnected = connected;

  if (connected &&
      !clockReadyPrinted &&
      time(nullptr) >= 1700000000) {
    clockReadyPrinted = true;

    Serial.println(
      "Clock ready. Hold D9 button for 5 seconds to send SOS."
    );
  }

  // Keep retrying regardless of sound switch position.
  if (!connected && now - lastWiFiRetry >= 30000) {
    lastWiFiRetry = now;

    Serial.println("WiFi: Retrying hotspot connection...");
    WiFi.reconnect();
  }
}

// ---------- Two sensors and distance beeps ----------
void updateObstacleAlert() {
  // Finish the current beep.
  if (beepOn &&
      millis() - lastBeepStart >= BEEP_DURATION_MS) {
    noTone(speaker);
    beepOn = false;
  }

  // Trigger one sensor at a time to reduce interference.
  if (millis() - lastMeasurement >= SENSOR_INTERVAL_MS) {
    lastMeasurement = millis();

    int sensorIndex = nextSensor;

    int activeTrig = (sensorIndex == 0) ? trigPin : trigPin2;
    int activeEcho = (sensorIndex == 0) ? echoPin : echoPin2;

    digitalWrite(activeTrig, LOW);
    delayMicroseconds(2);

    digitalWrite(activeTrig, HIGH);
    delayMicroseconds(10);

    digitalWrite(activeTrig, LOW);

    unsigned long duration = pulseIn(
      activeEcho,
      HIGH,
      30000UL
    );

    sensorUpdatedAt[sensorIndex] = millis();

    // Timeout is invalid, not zero centimeters.
    sensorValid[sensorIndex] = (duration > 0);

    if (sensorValid[sensorIndex]) {
      sensorDistance[sensorIndex] =
        duration * 0.0343f / 2.0f;
    }

    nextSensor = 1 - nextSensor;
  }

  unsigned long now = millis();

  // Select the closest reading within its sensor's alert range.
  validReading = false;
  distance = 0;

  for (int i = 0; i < 2; i++) {
    // Discard stale readings.
    if (sensorValid[i] &&
        now - sensorUpdatedAt[i] > READING_MAX_AGE_MS) {
      sensorValid[i] = false;
    }

    if (!sensorValid[i]) {
      continue;
    }

    bool withinAlertRange;

    if (i == 0) {
      // Sensor 1: below 375 cm.
      withinAlertRange =
        sensorDistance[i] < SILENT_DISTANCE_CM;
    } else {
      // Sensor 2: 50 cm or closer.
      withinAlertRange =
        sensorDistance[i] <= SENSOR2_ALERT_DISTANCE_CM;
    }

    if (withinAlertRange &&
        (!validReading || sensorDistance[i] < distance)) {
      distance = sensorDistance[i];
      validReading = true;
    }
  }

  // Show both readings and the selected alert distance.
  static unsigned long lastDistancePrint = 0;

  if (now - lastDistancePrint >= 500) {
    lastDistancePrint = now;

    Serial.print("Sensor 1: ");

    if (sensorValid[0]) {
      Serial.print(sensorDistance[0]);
      Serial.print(" cm");

      if (sensorDistance[0] >= SILENT_DISTANCE_CM) {
        Serial.print(" (outside alert range)");
      }
    } else {
      Serial.print("No valid echo");
    }

    Serial.print(" | Sensor 2: ");

    if (sensorValid[1]) {
      Serial.print(sensorDistance[1]);
      Serial.print(" cm");

      if (sensorDistance[1] > SENSOR2_ALERT_DISTANCE_CM) {
        Serial.print(" (outside 50 cm alert range)");
      }
    } else {
      Serial.print("No valid echo");
    }

    Serial.print(" | Closest within alert range: ");

    if (validReading) {
      Serial.print(distance);
      Serial.print(" cm");
    } else {
      Serial.print("None");
    }

    if (!soundEnabled) {
      Serial.print(" | Sound muted");
    }

    Serial.println();
  }

  // Silence when muted or neither sensor qualifies.
  // WiFi, SOS, and measurements continue.
  if (!soundEnabled || !validReading) {
    if (beepOn) {
      noTone(speaker);
    }

    beepOn = false;
    alertActive = false;
    return;
  }

  // Check again because pulseIn() may have taken time.
  if (beepOn &&
      now - lastBeepStart >= BEEP_DURATION_MS) {
    noTone(speaker);
    beepOn = false;
  }

  unsigned long beepInterval;

  if (distance <= URGENT_DISTANCE_CM) {
    // Rapid beeps for sensor 1 within 100 cm,
    // or sensor 2 within 50 cm.
    beepInterval = 120;
  } else {
    // Only sensor 1 can qualify at these distances.
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
  delay(2000);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(button, INPUT_PULLUP);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);

  pinMode(light, OUTPUT);
  pinMode(speaker, OUTPUT);

  digitalWrite(trigPin, LOW);
  digitalWrite(trigPin2, LOW);
  digitalWrite(light, LOW);
  digitalWrite(speaker, LOW);

  // Start muted until the switch has been debounced.
  setSoundEnabled(false);
  lastSwitchReading = digitalRead(switchPin);
  switchChangedAt = millis();

  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  client.setHandshakeTimeout(15);

  BaseType_t result = xTaskCreate(
    sosWorker,
    "SOSSender",
    12288,
    nullptr,
    1,
    &sosTaskHandle
  );

  if (result != pdPASS) {
    sosTaskHandle = nullptr;

    Serial.println(
      "ERROR: SOS sender failed; sensor functions remain available."
    );
  }

  // WiFi starts regardless of the sound switch.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  lastWiFiRetry = millis();

  Serial.println("WiFi: Connecting to hotspot...");

  // Synchronize time for Telegram certificate verification.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  updateSwitch();
  updateButton();
  updateWiFi();
  updateObstacleAlert();

  delay(1);
}
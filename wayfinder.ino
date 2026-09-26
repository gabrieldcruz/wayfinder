const int trigPin = 12;
const int echoPin = 11;
const int light = 5;
const int button = 9;
const int speaker = 10;

void setup() {
  Serial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(light, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  pinMode(speaker, OUTPUT);
}

void loop() {
  /*digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000);

  // Light stays on while the button is pressed.
  if (digitalRead(button) == LOW) {
    digitalWrite(light, HIGH);
  } else {
    digitalWrite(light, LOW);
  }

  if (duration > 0) {
    float distance = (duration * 0.0343) / 2;
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");
  } else {
    Serial.println("No echo detected");
  }

  delay(50);*/

  // Tone 1: 1000 Hz for 150 milliseconds
  tone(speaker, 1000);
  delay(150);
  
  // Tone 2: 1500 Hz for 250 milliseconds
  tone(speaker, 1500);
  delay(250);
  
  // Turn off the sound
  noTone(speaker);
  
  // Wait 5 seconds before repeating the alert
  delay(500);
}
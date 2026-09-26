const int trigPin = 12;
const int echoPin = 11;
const int light = 5;
const int button = 9;

void setup() {
  Serial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(light, OUTPUT);
  pinMode(button, INPUT_PULLUP);
}

void loop() {
  digitalWrite(trigPin, LOW);
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

  delay(50);
}
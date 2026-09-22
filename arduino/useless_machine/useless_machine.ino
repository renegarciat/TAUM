/*
  The almost useless machine
  A doorbell that doesn't ring when you press it, until you leave the doorway.

  Inputs:  a push button, a Grove Ultrasonic Ranger (single-pin SIG) distance sensor.
  Output:  a buzzer.

  Logic:
    1. Press the button *while* the sensor detects presence -> nothing
       happens, the press is just remembered.
    2. Once presence is then lost (the visitor leaves), a countdown starts.
    3. When the countdown finishes, the buzzer finally rings.
  Pressing the button while nobody is close does nothing at all.

  Wiring (3-pin Grove-style modules, all 3.3V, change the pin numbers below to match your board):
    Button     SIG -> GPIO4, VCC -> 3V3, GND -> GND (3-pin button module)
    Ultrasonic SIG -> GPIO2, VCC -> 3V3, GND -> GND (Grove Ultrasonic Ranger V2.0 supports 3.3V operation;
                                                      its single SIG pin connects directly to the ESP32 input,
                                                      no level shifting needed)
    Buzzer     SIG -> GPIO5, VCC -> 3V3, GND -> GND (3.3V buzzer module)
*/

// ---- Pins ----
const uint8_t BUTTON_PIN     = 4;
const uint8_t ULTRASONIC_PIN = 2;  // shared trigger/echo pin
const uint8_t BUZZER_PIN     = 5;

// ---- Tunables ----
const float PRESENCE_THRESHOLD_CM  = 50.0;   // distances at or below this count as "someone is here"
const unsigned long LEAVE_DELAY_MS = 5000;   // delay after leaving before the buzzer fires
const unsigned long BUZZ_DURATION_MS = 5000; // how long the buzzer rings once triggered

const unsigned long LOOP_PERIOD_MS       = 100;
const unsigned long BUTTON_DEBOUNCE_MS   = 30;
const uint8_t PRESENCE_DEBOUNCE_SAMPLES  = 3;  // consecutive matching reads before believing a presence change
const unsigned long ECHO_TIMEOUT_US      = 30000; // ~5 m round trip; also covers "nothing in range"
const uint8_t BUZZER_DUTY                = 10;   // analogWrite duty (0-255) used while the buzzer is "on";
                                                  // this buzzer only sounds at a low duty, not at e.g. 128

enum MachineState {
  STATE_IDLE,       // nothing stored, waiting for a button press while someone is near
  STATE_ARMED,      // button was pressed while someone was near; waiting for them to leave
  STATE_COUNTDOWN,  // they left; timer running before the buzzer fires
  STATE_BUZZING,    // the buzzer is doing its belated job
};

MachineState state = STATE_IDLE;
unsigned long countdownDeadlineMs = 0;
unsigned long buzzStopMs = 0;

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT); // self-powered button module drives its own idle level, no internal pull-up
  pinMode(ULTRASONIC_PIN, OUTPUT);
  digitalWrite(ULTRASONIC_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, 0);

  Serial.println("The almost useless machine is ready. Press the button while standing close.");
}

// Blocking Grove Ultrasonic Ranger read (single shared SIG pin: trigger out, then echo in).
// Returns distance in cm, or -1 if no echo (out of range / no object).
float measureDistanceCm() {
  pinMode(ULTRASONIC_PIN, OUTPUT);
  digitalWrite(ULTRASONIC_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(ULTRASONIC_PIN, LOW);

  pinMode(ULTRASONIC_PIN, INPUT);
  unsigned long duration = pulseIn(ULTRASONIC_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0;
  }
  return duration * 0.01715f; // speed of sound (343 m/s) / 2, applied to round-trip us
}

// Debounced presence read: requires several consecutive matching samples before flipping.
bool presenceDebounced(float distanceCm) {
  static bool confirmed = false;
  static bool candidate = false;
  static uint8_t matches = 0;

  bool rawPresence = (distanceCm > 0) && (distanceCm <= PRESENCE_THRESHOLD_CM);

  if (rawPresence == candidate) {
    if (matches < PRESENCE_DEBOUNCE_SAMPLES) {
      matches++;
    }
  } else {
    candidate = rawPresence;
    matches = 1;
  }

  if (matches >= PRESENCE_DEBOUNCE_SAMPLES) {
    confirmed = candidate;
  }
  return confirmed;
}

// Edge-triggered, debounced button read. Returns true exactly once per physical press.
// This is a self-powered 3-pin button module: SIG idles LOW and goes HIGH when pressed.
bool buttonPressedEdge() {
  static bool stableState = false; // false = released (idle LOW)
  static bool lastRaw = false;
  static unsigned long lastChangeMs = 0;

  bool raw = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }

  if (raw != stableState && (now - lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
    bool wasReleaseToPress = (stableState == false && raw == true);
    stableState = raw;
    return wasReleaseToPress;
  }
  return false;
}

void loop() {
  float distance = measureDistanceCm();
  bool present = presenceDebounced(distance);
  bool pressed = buttonPressedEdge();

  switch (state) {
    case STATE_IDLE:
      if (pressed && present) {
        Serial.println("Button pressed while someone is near. Remembering that, doing nothing else.");
        state = STATE_ARMED;
      } else if (pressed) {
        Serial.println("Button pressed, but nobody's close enough. Ignored.");
      }
      break;

    case STATE_ARMED:
      if (!present) {
        Serial.println("They left. Starting the countdown...");
        countdownDeadlineMs = millis() + LEAVE_DELAY_MS;
        state = STATE_COUNTDOWN;
      }
      break;

    case STATE_COUNTDOWN:
      if (millis() >= countdownDeadlineMs) {
        Serial.println("Ding dong. Too little, too late.");
        analogWrite(BUZZER_PIN, BUZZER_DUTY);
        buzzStopMs = millis() + BUZZ_DURATION_MS;
        state = STATE_BUZZING;
      }
      break;

    case STATE_BUZZING:
      if (millis() >= buzzStopMs) {
        analogWrite(BUZZER_PIN, 0);
        Serial.println("Done. Back to waiting for the next victim.");
        state = STATE_IDLE;
      }
      break;
  }

  delay(LOOP_PERIOD_MS);
}

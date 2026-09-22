# The almost useless machine
A doorbell that doesn't ring when you press it, until you leave the doorway.

## How it works

Two inputs, one output, one grudge:

- **Push button** — someone at the door presses it.
- **Ultrasonic sensor (HC-SR04)** — detects whether someone is standing close (presence).
- **Buzzer** — the "doorbell", such as it is.

The logic:

1. Press the button *while* the sensor detects presence → nothing happens. The
   press is just remembered.
2. Only once presence is then lost (the visitor gives up and leaves) does a
   countdown start.
3. When the countdown finishes, the buzzer finally rings — well after the
   visitor is gone.

Pressing the button while nobody is detected as close does nothing at all
(not even a stored press): you have to be standing there for the machine to
bother remembering you.

## Wiring (default pins, configurable via `idf.py menuconfig` → "Useless Machine Configuration")

| Signal        | GPIO | Notes                                                                  |
| ------------- | ---- | ----------------------------------------------------------------------|
| Button        | 4    | Other leg to GND; internal pull-up enabled, active-low.               |
| HC-SR04 TRIG  | 18   | 3.3V logic out, fine to drive the sensor's trigger pin directly.      |
| HC-SR04 ECHO  | 19   | HC-SR04 echo is 5V — use a voltage divider (e.g. 1k/2k) down to 3.3V. |
| Buzzer        | 13   | Assumes an active buzzer module (onboard driver transistor).          |

The HC-SR04 itself needs 5V on its VCC pin to work correctly; only its ECHO
output needs to be stepped down before reaching the ESP32.

## Tunables (Kconfig)

- Presence threshold distance (default 50 cm)
- Delay after the visitor leaves, before the buzzer fires (default 5 s)
- Number of buzzer beeps and their duration (default 3 beeps, 200 ms each)

// OpenXilEdu pedal reader - runs on the Arduino, read by ExtProc_ArduinoSerialBridge.cpp
// on the PC side over USB serial.
//
// Wiring:
//   Potentiometer (3-pin): outer pins to 5V and GND (either way round -
//   it just flips which direction is "more throttle"), middle pin
//   (the wiper) to A0.
//
//   No LED wiring needed - the ignition indicator uses the board's
//   built-in LED (the one labeled "L", next to pin 13).
//
// Protocol (must match ExtProc_ArduinoSerialBridge.cpp on the PC side):
//   - Every loop, sends "PedalPos:<0-1023>\n" (the raw analogRead() value).
//   - Reads back "FireUp:<0 or 1>\n" whenever the PC sends it, and drives
//     the built-in LED accordingly.
//   - Baud rate (115200) must match the `baud` value hardcoded in
//     ExtProc_ArduinoSerialBridge.cpp - if you change one, change the other.

int potPin = A0;
int potVal = 0;
int ledPin = LED_BUILTIN;

void setup() {
  Serial.begin(115200); // match ExtProc_ArduinoSerialBridge.cpp's baud rate
  pinMode(ledPin, OUTPUT);
}

void loop() {
  potVal = analogRead(potPin);

  Serial.print("PedalPos:");
  Serial.println(potVal);

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    if (cmd.startsWith("FireUp:")) {
      int value = cmd.substring(7).toInt();
      digitalWrite(ledPin, value >= 1 ? HIGH : LOW);
    }
  }
}

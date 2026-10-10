#include <HardwareSerial.h> // Or use HardwareSerial if mapping secondary UART

const int LED_PIN = 1;       // MOSFET Gate pin
const unsigned long FLASH_DURATION = 150; // How long the LED flash stays on (ms)

bool flashing = false;
unsigned long flashStartTime = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize Serial for JDY-40 / debugging
  Serial.begin(9600); 
  // Initialize second serial or software serial for DFPlayer if needed
}

void loop() {
  // 1. Non-blocking check for incoming wireless trigger from JDY-40
  if (Serial.available() > 0) {
    char incomingChar = Serial.read();
    
    // Check for your designated trigger character (e.g., 'X' for explosion)
    if (incomingChar == 'X') {
      triggerExplosionSequence();
    }
  }

  // 2. Non-blocking state management for the LED flash effect
  if (flashing && (millis() - flashStartTime >= FLASH_DURATION)) {
    digitalWrite(LED_PIN, LOW);
    flashing = false;
  }
}

void triggerExplosionSequence() {
  // Turn on LED flash via MOSFET
  digitalWrite(LED_PIN, HIGH);
  flashStartTime = millis();
  flashing = true;

  // Send serial command to DFPlayer Mini to play track 001
  // e.g., myDFPlayer.play(1); 
}

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define DFPLAYER_BUSY_PIN   GPIO_NUM_7
#define DFPLAYER_TX_PIN     GPIO_NUM_12
#define DFPLAYER_RX_PIN     GPIO_NUM_13
#define JDY_P1_PIN          GPIO_NUM_8
#define JDY_SET_PIN         GPIO_NUM_6
#define JDY_RX_PIN          GPIO_NUM_4
#define JDY_TX_PIN          GPIO_NUM_5

HardwareSerial DFPLayerSerial(2);  // Use UART2
HardwareSerial JDY_serial(1);      // Use UART1
DFRobotDFPlayerMini myDFPlayer;

unsigned long last_activity = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  while(!myDFPlayer.begin(DFPLayerSerial)){
    Serial.println("DFPlayer Mini not detected!");
    delay(500);
  }
  Serial.println("DFPlayer Mini ready!");

  Serial.println("Will check BUSY pin");
  pinMode(DFPLAYER_BUSY_PIN, INPUT);

  Serial.println("Will check JDY pin");
  pinMode(JDY_P1_PIN, INPUT);

  Serial.println("JDY SET PIN LOW");
  JDY_serial.begin(9600, SERIAL_8N1, JDY_RX_PIN, JDY_TX_PIN);
  pinMode(JDY_SET_PIN, OUTPUT);
  digitalWrite(JDY_SET_PIN, LOW);
  delay(100);
  if(JDY_serial.available() > 0){
      char c = JDY_serial.read();
      Serial.print(c);
  }
  sendAtCommand("AT+VER");
  sendAtCommand("AT+POWE9");
  sendAtCommand("AT+RFID0017");
  sendAtCommand("AT+DVID0017");
  sendAtCommand("AT+RFC017");
  sendAtCommand("AT+CLSS");
  sendAtCommand("AT+CLSSC2");

  Serial.println("Switching back to Transparent Transmission Mode...");
  digitalWrite(JDY_SET_PIN, HIGH);
  delay(100);
}

void loop() {
  int buttonPressed = digitalRead(JDY_P1_PIN);
  //Serial.print("DFPlayer is IDLE (Not Busy), HIGH:");
  //Serial.println(buttonPressed);
  if(buttonPressed == HIGH){
    if(!is_playing()){
      play_something(1);
    } else {
      Serial.println("already playing! (BUTTON)");
    }
  }
  if(is_playing()){
    if(millis() - last_activity > 100){
      last_activity = millis();
      Serial.println("DFPlayer is PLAYING (Busy)");
    }
  }
  while (JDY_serial.available() > 0) {
    String line = JDY_serial.readStringUntil('\n');
    line.trim();
    Serial.print("UART GOT: ");
    Serial.println(line);
    if(line == "BANG"){
      if(!is_playing()){
        play_something(1);
      } else {
        Serial.println("already playing! (UART=BANG)");
      }
    }
  }
}

int8_t is_playing() {
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW)
    return 1;
  return 0;
}

void play_something(uint16_t nr) {
  last_activity = millis();
  myDFPlayer.volume(25); // Volume 0-30
  Serial.print("Playing File: ");
  Serial.println(nr);
  myDFPlayer.play(nr);
}

// Helper function to cleanly send an AT command and log it
void sendAtCommand(String command) {
  Serial.print("Sending: ");
  Serial.println(command);
  JDY_serial.println(command);
  
  // Wait a moment for the module to process and reply
  delay(300);
  
  // Read any immediate response lines
  while (JDY_serial.available() > 0) {
    String line = JDY_serial.readStringUntil('\n');
    Serial.print("Response: ");
    Serial.println(line);
  }
  Serial.println("-------------------");
}

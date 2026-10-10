#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define DFPLAYER_BUSY_PIN   GPIO_NUM_7
#define DFPLAYER_TX_PIN     GPIO_NUM_12
#define DFPLAYER_RX_PIN     GPIO_NUM_13
#define JDY_PIN             GPIO_NUM_8

HardwareSerial DFPLayerSerial(2);  // Use UART2
DFRobotDFPlayerMini myDFPlayer;

void setup() {
  Serial.begin(115200);

  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  while(!myDFPlayer.begin(DFPLayerSerial)){
    Serial.println("DFPlayer Mini not detected!");
    delay(500);
  }
  Serial.println("DFPlayer Mini ready!");

  Serial.println("Will check BUSY pin");
  pinMode(DFPLAYER_BUSY_PIN, INPUT);

  Serial.println("Will check JDY pin");
  pinMode(JDY_PIN, INPUT);
}

void loop() {
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW){
    Serial.println("DFPlayer is PLAYING (Busy)");
    delay(100);
  } else {
    int buttonPressed = digitalRead(JDY_PIN);
    Serial.print("DFPlayer is IDLE (Not Busy), pressed:");
    Serial.println(buttonPressed);
    if(buttonPressed){
      myDFPlayer.volume(25); // Volume 0-30
      Serial.println("Playing File 001.mp3");
      myDFPlayer.play(1);
    } else {
      delay(100);
    }
  }     
}

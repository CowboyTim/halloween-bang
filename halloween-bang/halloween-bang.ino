#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

HardwareSerial mySerial(1);  // Use UART1
DFRobotDFPlayerMini myDFPlayer;

void setup() {
  Serial.begin(115200);
  mySerial.begin(9600, SERIAL_8N1, GPIO_NUM_13, GPIO_NUM_12);

  if (!myDFPlayer.begin(mySerial)) {
    Serial.println("DFPlayer Mini not detected!");
    while (1)
      ;
  }

  Serial.println("DFPlayer Mini ready!");
  myDFPlayer.volume(25);  // Volume level (0–30)
  Serial.println("Playing File 001.mp3");
  myDFPlayer.play(1);  // Play first track (0001.mp3)
}

void loop() {
  // Nothing in loop
}

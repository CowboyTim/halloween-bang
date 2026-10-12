#include <EdSoftLED.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <driver/rmt.h>

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define DFPLAYER_BUSY_PIN   GPIO_NUM_7
#define DFPLAYER_TX_PIN     GPIO_NUM_12
#define DFPLAYER_RX_PIN     GPIO_NUM_13
#define JDY_P1_PIN          GPIO_NUM_8
#define JDY_SET_PIN         GPIO_NUM_6
#define JDY_RX_PIN          GPIO_NUM_4
#define JDY_TX_PIN          GPIO_NUM_5

#define BUILTIN_LED         GPIO_NUM_48
#define BUTTON_PIN          GPIO_NUM_8


#define LED_STRIP_GPIO_PIN  GPIO_NUM_4  // data pin to DIN on the strip
#define NUM_LEDS                   300  // can be shorter
EdSoftLED strip(NUM_LEDS, LED_STRIP_GPIO_PIN, WS2812RGB);

HardwareSerial DFPLayerSerial(2);  // Use UART2
DFRobotDFPlayerMini myDFPlayer;
#ifdef JDY40
HardwareSerial JDY_serial(1);      // Use UART1
#endif



unsigned long last_activity = 0;
uint8_t receiverAddress[] = {0xe4, 0xb3, 0x23, 0xf9, 0x43, 0xf0};

void OnDataRecv(const esp_now_recv_info_t *mac, const uint8_t *incomingData, int len) {
  char incomingString[32];
  int dataLen = len < sizeof(incomingString) - 1 ? len : sizeof(incomingString) - 1;
  memcpy(incomingString, incomingData, dataLen);
  incomingString[dataLen] = '\0'; // Null-terminate string

  if (String(incomingString) == "BANG") {
    Serial.println("Received BANG via ESP-NOW!");
    if(!is_playing()){
      play_something(1);
    } else {
      Serial.println("is already playing!");
    }
  }
}

void setup() {
  Serial.begin(115200);

  if (!rmtInit(LED_STRIP_GPIO_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
    Serial.println("init sender failed\n");
  }

  // enable WiFi-now
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

#if defined(BANGER)
  // DFPPlayer serial setup
  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  while(!myDFPlayer.begin(DFPLayerSerial)){
    Serial.println("DFPlayer Mini not detected!");
    delay(500);
  }
  Serial.println("DFPlayer Mini ready!");
  Serial.print("Will check BUSY pin number: ");
  Serial.println(DFPLAYER_BUSY_PIN);
  pinMode(DFPLAYER_BUSY_PIN, INPUT);

  // JDY-40 setup
#ifdef JDY40
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
#endif

  // for BANGER: just a callback
  esp_now_register_recv_cb(OnDataRecv);
#else
  // Register peer
  // for BUTTON: target receiverAddress
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.print("button INPUT_PULLUP PIN: ");
  Serial.println(BUTTON_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif

  // Fancy led strip
  setup_led_strip();
}

void loop() {
#if defined(BANGER)
#if defined(JDY40)
  int r_received = digitalRead(JDY_P1_PIN);
  //Serial.print("DFPlayer is IDLE (Not Busy), HIGH:");
  //Serial.println(r_received);
  if(r_received == HIGH){
    if(!is_playing()){
      play_something(1);
    } else {
      Serial.println("already playing! (BUTTON)");
    }
  }
#endif
  if(is_playing()){
    if(millis() - last_activity > 100){
      last_activity = millis();
      Serial.println("DFPlayer is playing (Busy)");
    }
  }
#else
  int buttonPressed = digitalRead(BUTTON_PIN);
  //Serial.print("button value: ");
  //Serial.println(buttonPressed);
  if(buttonPressed == LOW){
    Serial.println("sending BANG");
    char messaggio[] = "BANG";
    esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
    // debounce button
    delay(500);
  }
#endif

#ifdef JDY40
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
#endif
}

int8_t is_playing() {
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW)
    return 1;
  return 0;
}

void play_something(uint16_t nr) {
  run_ledstrip_animation();
  last_activity = millis();
  myDFPlayer.volume(30); // Volume 0-30
  Serial.print("Playing File: ");
  Serial.println(nr);
  myDFPlayer.play(nr);
}

void setup_led_strip(){
  strip.begin();           // Claims the RMT channel safely under Core 3.x
  strip.setBrightness(10); // Keep current draw safe
  strip.clear();
  strip.show();
}

void run_ledstrip_animation() {
  // Sweep the spark from start to end (+ tail offset),
  // pos+=5 to PACE Fast! see also the delay(5)
  for (int pos = 0; pos < NUM_LEDS + 10; pos+=5) {
    
    // 1. Clear the internal buffer instantly (uses fast memory clearing)
    strip.clear();

    // 2. Draw only the active tail window (much faster than looping all 300 LEDs)
    int tailLength = 10;
    for (int i = 0; i < tailLength; i++) {
      int index = pos - i;
      
      if (index >= 0 && index < NUM_LEDS) {
        if (i == 0) {
          // Hot white-yellow head
          strip.setPixelColor(index, strip.Color(255, 180, 50)); 
        } else {
          // Fading fiery tail
          int scaleFactor = 255 - (i * (255 / tailLength));
          uint8_t r = (255  * scaleFactor) / 255;
          uint8_t g = (100  * scaleFactor) / 255;
          uint8_t b = (0    * scaleFactor) / 255;
          
          strip.setPixelColor(index, strip.Color(r, g, b));
        }
      }
    }

    // 3. Add random sputtering sparks along the fuse (classic crackling fuse effect)
    for (int s = 0; s < 4; s++) {
      int randomSpark = random(NUM_LEDS);
      // Only drop a spark near the active burning area or behind it
      if (randomSpark < pos && randomSpark > pos - 25) {
        // Random bright yellow/white spark flash
        strip.setPixelColor(randomSpark, strip.Color(255, 255, 150));
      }
    }

    // Push the updated buffer to the physical strip via RMT
    strip.show();
    
    // Optional: tiny delay if you want to pace it, or remove entirely for max speed
    // delay(5); 
  }
}

#ifdef JDY40
void sendAtCommand(String command) {
  Serial.print("Sending: ");
  Serial.println(command);
  JDY_serial.println(command);
  
  delay(300);
  
  while (JDY_serial.available() > 0) {
    String line = JDY_serial.readStringUntil('\n');
    Serial.print("Response: ");
    Serial.println(line);
  }
  Serial.println("-------------------");
}
#endif

#ifndef UART_LOG_DEV_UART0
#define UART_LOG_DEV_UART0 1
#endif

#if UART_LOG_DEV_UART0 == 1
 #define NO_GLOBAL_INSTANCES
 #define NO_GLOBAL_SERIAL
 #include <HardwareSerial.h>
 extern HardwareSerial Serial0;
 extern HardwareSerial Serial1;
 #define UART0         Serial0
 #define USBSERIAL0    Serial0
 #define Serial        Serial0
 #define UART1         Serial1
#else
 #include <HardwareSerial.h>
 #define UART0         Serial
 #define USBSERIAL0    Serial
 #define UART1         Serial1
#endif // UART_LOG_DEV_UART0

#include <EdSoftLED.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <driver/rmt.h>

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define doYIELD yield();

#define DFPLAYER_BUSY_PIN   GPIO_NUM_7
#define DFPLAYER_TX_PIN     GPIO_NUM_12
#define DFPLAYER_RX_PIN     GPIO_NUM_13

#define BUILTIN_LED         GPIO_NUM_21
#define BUTTON_PIN          GPIO_NUM_8

#define LED_STRIP_GPIO_PIN  GPIO_NUM_4  // data pin to DIN on the strip
#define NUM_LEDS                   300  // can be shorter
EdSoftLED strip(NUM_LEDS, LED_STRIP_GPIO_PIN, WS2812RGB);

HardwareSerial DFPLayerSerial(2);  // Use UART2
DFRobotDFPlayerMini myDFPlayer;



uint8_t do_light_sleep = 0;
uint8_t animation_is_running = 0;
uint8_t trigger_animation = 0;
unsigned long last_activity = 0;
uint8_t receiverAddress[] = {0xe4, 0xb3, 0x23, 0xf9, 0x43, 0xf0};

void OnDataRecv(const esp_now_recv_info_t *mac, const uint8_t *msg, int len) {
  if(strncmp((const char *)msg, "BANG", len) == 0)
    trigger_animation = 1; 
}

void setup() {
  Serial.begin(115200);

  if (!rmtInit(LED_STRIP_GPIO_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
    Serial.println("init sender failed\n");
  }

  // enable WiFi-now
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

#if defined(BANGER)
  // DFPPlayer serial setup
  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  while(!myDFPlayer.begin(DFPLayerSerial)){
    doYIELD;
    Serial.println("DFPlayer Mini not detected!");
    delay(500);
  }
  Serial.println("DFPlayer Mini ready!");
  Serial.print("Will check BUSY pin number: ");
  Serial.println(DFPLAYER_BUSY_PIN);
  pinMode(DFPLAYER_BUSY_PIN, INPUT);

  // for BANGER: just a callback
  esp_now_register_recv_cb(OnDataRecv);

  // Fancy led strip
  setup_led_strip();
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

  bool ok_btn = esp_sleep_is_valid_wakeup_gpio((gpio_num_t)BUTTON_PIN);
  if(ok_btn){
    esp_err_t err = gpio_wakeup_enable(BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    if(err != ESP_OK){
      Serial.print("not using light sleep: ");
      Serial.println(esp_err_to_name(err));
    } else {
      err = esp_sleep_enable_gpio_wakeup();
      if(err != ESP_OK){
        Serial.print("not using light sleep, gpio pin not ok: ");
        Serial.println(esp_err_to_name(err));
      } else {
        // Correct GPIO config for button pin PULLUP/DOWN, as we GND the button
        err = gpio_pullup_en((gpio_num_t)BUTTON_PIN);
        if(err == ESP_OK) {
          err = gpio_pulldown_dis((gpio_num_t)BUTTON_PIN);
          if(err == ESP_OK) {
            do_light_sleep = 1;
            Serial.println("will attempt light sleep");
          }
        }
      }
    }
  } else {
    Serial.println("button cant be used for light sleep");
  }
#endif

  // builtin LED (fails for the EdSoftLED's use of RMT)
  //neopixelWrite(BUILTIN_LED, 0, 0, 200); // (Pin, Green, Red, Blue)
}

void loop() {
#if defined(BANGER)
  // BANGER just logs some info, the OnDataRecv handles the real thing
  if(trigger_animation == 1){
    trigger_animation = 0;
    Serial.println("received DEMOLITION request!");
    if(!is_playing()){
      play_something();
    } else {
      Serial.println("animation already running");
    }
  }
  if(is_playing()){
    if(millis() - last_activity > 100){
      last_activity = millis();
      Serial.println("DFPlayer is playing (Busy)");
    }
  }
#else
  // sleep
  if(do_light_sleep == 1){
    Serial.println("sleepy sleepy");
    Serial.flush();
    esp_err_t err = esp_light_sleep_start();
    if(err != ESP_OK){
      Serial.print("problem light sleep: ");
      Serial.println(esp_err_to_name(err));
      do_light_sleep = 0;
    } else {
      WiFi.mode(WIFI_STA);
      delay(15); // Let the radio stabilize
      Serial.flush();
      do_light_sleep = 1;
      Serial.println("yawn, wakeup, sending BANG");
      char messaggio[] = "BANG";
      esp_err_t err = esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
      if(err == ESP_OK){
        Serial.println("BANG sent!");
      } else {
        Serial.print("problem sending BANG: ");
        Serial.println(esp_err_to_name(err));
      }
    }
  }

  if(do_light_sleep == 0){
    int buttonPressed = digitalRead(BUTTON_PIN);
    //Serial.print("button value: ");
    //Serial.println(buttonPressed);
    if(buttonPressed == LOW){
      Serial.println("ADHD style sending BANG");
      char messaggio[] = "BANG";
      esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
    } else {
      delay(1);
      return;
    }
  }
  
  // debounce button
  delay(500);
  /*
  while (digitalRead(BUTTON_PIN) == LOW) {
    doYIELD;
    delay(10);
  }
  */
#endif
}

int8_t is_playing() {
  if(animation_is_running)
    return 1;
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW)
    return 1;
  return 0;
}

void play_something() {
  animation_is_running = 1;
  // ledstrip
  run_ledstrip_animation();

  // sound effect
  last_activity = millis();
  myDFPlayer.volume(30); // Volume 0-30
  Serial.println("Playing File: 1");
  myDFPlayer.play(1);
  animation_is_running = 0;
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
    doYIELD;
    
    // 1. Clear the internal buffer instantly (uses fast memory clearing)
    strip.clear();

    // 2. Draw only the active tail window (much faster than looping all 300 LEDs)
    int tailLength = 10;
    for (int i = 0; i < tailLength; i++) {
      doYIELD;
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
      doYIELD;
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

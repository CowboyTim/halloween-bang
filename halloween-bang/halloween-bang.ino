#include <EdSoftLED.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <driver/rmt.h>
#include <esp_task_wdt.h>

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define DFPLAYER_BUSY_PIN   GPIO_NUM_7
#define DFPLAYER_TX_PIN     GPIO_NUM_12
#define DFPLAYER_RX_PIN     GPIO_NUM_13

#define BUILTIN_LED         GPIO_NUM_21
#define BUTTON_PIN          GPIO_NUM_8

#define doYIELD myYIELD()

#define LINE_START    0
#define LINE_END    111
#define RING_START  112
#define RING_END    299

#define LED_STRIP_GPIO_PIN  GPIO_NUM_4  // data pin to DIN on the strip
#define NUM_LEDS                   300  // can be shorter
EdSoftLED strip(NUM_LEDS, LED_STRIP_GPIO_PIN, WS2812RGB);

HardwareSerial DFPLayerSerial(2);  // Use UART2
DFRobotDFPlayerMini myDFPlayer;

#define WATCHDOG_TIMEOUT_MS 60000   // 60s
esp_task_wdt_user_handle_t wdt_user_handle;

uint8_t animation_is_running = 0;
uint8_t trigger_animation = 0;
unsigned long last_activity = 0;
uint8_t receiverAddress[] = {0xe4, 0xb3, 0x23, 0xf9, 0x43, 0xf0};

void setup_cpu_speed(uint32_t freq_mhz = 160) {
  uint32_t xtal_f = getXtalFrequencyMhz();
  uint32_t cpu_f = getCpuFrequencyMhz();
  Serial.print("[ESP] Current CPU frequency: ");
  Serial.print(cpu_f);
  Serial.println(" MHz");
  if(xtal_f != 0) {
    Serial.print("[ESP] Detected XTAL frequency: ");
    Serial.print(xtal_f);
    Serial.println(" MHz");
    if(xtal_f != 40) {
      Serial.println("[ESP] Warning: Unusual XTAL frequency detected, expected 40 MHz");
    } else {
      if(xtal_f == 40) {
        // 40 MHz XTAL, set CPU to 80, 160
        // 40 MHz XTAL also allows 10, 20, 40 MHz CPU
        cpu_f = freq_mhz;
        Serial.print("[ESP] Setting CPU frequency to ");
        Serial.print(cpu_f);
        Serial.print("MHz for ");
        Serial.print(xtal_f);
        Serial.println("MHz XTAL");
        setCpuFrequencyMhz(cpu_f);
      }
      cpu_f = getCpuFrequencyMhz();
      Serial.print("[ESP] Current CPU frequency: ");
      Serial.print(cpu_f);
      Serial.println(" MHz");
    }
  } else {
    Serial.println("[ESP] Failed to detect XTAL frequency, defaulting to 40 MHz");
  }
}

void OnDataRecv(const esp_now_recv_info_t *mac, const uint8_t *msg, int len) {
  if(strncmp((const char *)msg, "BANG", len) == 0) {
    trigger_animation = 1;
  }
}

void myYIELD(){
  yield();
  // Reset the watchdog timer
  if(wdt_user_handle != NULL){
      esp_err_t ok = esp_task_wdt_reset_user(wdt_user_handle);
      if(ok != ESP_OK) {
        Serial.print("[WDT] Failed to reset WDT: ");
        Serial.println(esp_err_to_name(ok));
      }
  }
}

uint8_t printed_ready = 0;
uint8_t button_was_pressed = 0;
portMUX_TYPE r_mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR button_pressed() {
  portENTER_CRITICAL_ISR(&r_mux);
  button_was_pressed = 1;
  portEXIT_CRITICAL_ISR(&r_mux);
}

void setup() {
  Serial.begin(115200);

  setup_wdt();

  // builtin LED (fails for the EdSoftLED's use of RMT)
  //neopixelWrite(BUILTIN_LED, 0, 0, 200); // (Pin, Green, Red, Blue)

  if (!rmtInit(LED_STRIP_GPIO_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
    Serial.println("init sender failed\n");
  }

  // enable WiFi-now
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

#if defined(BANGER)
  // Fancy led strip
  setup_led_strip();

  // DFPPlayer serial setup
  doYIELD;
  Serial.println("DFPlayer Mini WAIT 1");
  delay(1000);
  Serial.println("DFPlayer UART setup");
  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  Serial.println("DFPlayer Mini WAIT 2");
  delay(1000);
  Serial.println("DFPlayer Mini setup START");
  while(!myDFPlayer.begin(DFPLayerSerial, true, true)){
    doYIELD;
    Serial.println("DFPlayer Mini not detected!");
    delay(500);
  }
  doYIELD;
  Serial.println("DFPlayer Mini ready!");
  Serial.print("Will check BUSY pin number: ");
  Serial.println(DFPLAYER_BUSY_PIN);
  pinMode(DFPLAYER_BUSY_PIN, INPUT);
  while(digitalRead(DFPLAYER_BUSY_PIN) == LOW){
    doYIELD;
    Serial.println("DFPlayer BUSY pin still LOW, waiting");
    delay(100);
  }
  Serial.println("DFPlayer BUSY HIGH, continue");
  myDFPlayer.volume(10);
  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  myDFPlayer.enableDAC();
  Serial.println("DFPlayer setup complete");

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
  // Attach interrupt on FALLING edge (when the button pulls the pin to GND)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_pressed, FALLING);

  // minimal 80MHz, else ESP-NOW doesnt work on esp32s3
  setup_cpu_speed(80);

#endif
}

void loop() {
  doYIELD;

#if defined(BANGER)
  // BANGER just logs some info, the OnDataRecv handles the real thing
  if(trigger_animation == 1){
    trigger_animation = 0;
    Serial.println("received DEMOLITION request!");
    if(!animation_is_playing()){
      printed_ready = 0;
      play_something();
    } else {
      Serial.println("animation already running");
    }
  }
  if(animation_is_playing()){
    if(millis() - last_activity > 100){
      last_activity = millis();
      Serial.println("DFPlayer is playing (Busy)");
    }
  } else {
    if(!printed_ready){
      printed_ready = 1;
      Serial.println("READY TO DEMOLISH");
    }
  }
#else
  uint8_t do_send_bang = 0;
  portENTER_CRITICAL(&r_mux);
  if(button_was_pressed){
      button_was_pressed = 0;
      do_send_bang = 1;
  }
  portEXIT_CRITICAL(&r_mux);
  if(do_send_bang){
      Serial.println("ADHD style sending BANG");
      char messaggio[] = "BANG";
      esp_err_t err = esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
      if(err != ESP_OK){
        Serial.print("problem ESP-NOW send: ");
        Serial.println(esp_err_to_name(err));
      } else {
        Serial.println("BANG sent!");
      }
      // debounce button
      delay(500);
  } else {
    delay(5);
  }

#endif
}

uint8_t animation_is_playing() {
  if(animation_is_running)
    return 1;
  return sound_is_playing();
}

uint8_t sound_is_playing(){
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW)
    return 1;
  return 0;
}

void play_something() {
  animation_is_running = 1;
  last_activity = millis();

  // sound effect 1 (Whooosh)
  myDFPlayer.volume(30); // Volume 0-30
  Serial.println("Playing File: 1");
  myDFPlayer.play(1);

  // ledstrip clear, +1
  run_ledstrip_animation_1();

  while(sound_is_playing()){
    doYIELD;
    delay(1);
  }

  // sound effect (Kabooom)
  myDFPlayer.stop();
  myDFPlayer.volume(30); // Volume 0-30
  Serial.println("Playing File: 2");
  myDFPlayer.play(2);

  // ledstrip +2, clear
  run_ledstrip_animation_2();

  while(sound_is_playing()){
    doYIELD;
    delay(1);
  }

  // done
  Serial.println("done showing demolition animation");
  animation_is_running = 0;
  trigger_animation = 0;
  printed_ready = 0;
}

void setup_led_strip(){
  strip.begin();           // Claims the RMT channel safely under Core 3.x
  strip.setBrightness(10); // Keep current draw safe
  strip.clear();
  strip.show();
}

void run_ledstrip_animation_1() {
  // Sweep the spark from start to end (+ tail offset),
  // pos+=5 to PACE Fast! see also the delay(5)
  Serial.println("RUNNING Animation LED 1");
  for (int pos = LINE_START; pos < LINE_END + 10; pos+=3) {
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
  strip.clear();
  strip.show();
  Serial.println("RUNNING Animation LED 1: DONE");
}

void run_ledstrip_animation_2() {
  Serial.println("RUNNING Animation LED 2");
  for (int frame = 0; frame < 60; frame++) {
    doYIELD;
    strip.clear();

    // The line is now burnt out / dark, but let's make the ring go wild
    for (int rIdx = RING_START; rIdx <= RING_END; rIdx++) {
      // Random erratic glowing across the 1m ring
      int intensity = random(80, 255);
      strip.setPixelColor(rIdx, strip.Color(intensity, intensity / 3, 0));
    }

    strip.show();
    delay(5);
  }
  strip.clear();
  strip.show();
  Serial.println("RUNNING Animation LED 2: DONE");
}

void setup_wdt(){
  esp_err_t ok;

  // First, deinitialize the Arduino core's WDT if it exists
  esp_task_wdt_deinit();

  // Now initialize with our custom config
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WATCHDOG_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic = false,  // Set to false to allow custom handler to be called
  };
  ok = esp_task_wdt_init(&wdt_config);
  if(ok != ESP_OK) {
    Serial.print("Failed to initialize task watchdog: ");
    Serial.println(esp_err_to_name(ok));
    return;
  }
  Serial.println("[WDT] Task watchdog initialized");

  // Register watchdog timeout callback
  ok = esp_task_wdt_add_user((char*)"main", &wdt_user_handle);
  if(ok != ESP_OK) {
    Serial.print("[WDT] Failed to register task: ");
    Serial.println(esp_err_to_name(ok));
  } else {
    Serial.println("[WDT] Watchdog timeout callback registered successfully");
  }
}

void esp_task_wdt_isr_user_handler(void){
  // CRITICAL: Must use abort() not esp_restart() in ISR context!
  // esp_restart() is NOT ISR-safe and will trigger Interrupt WDT
  Serial.println("[WDT] 1 Task watchdog timeout occurred!");
}


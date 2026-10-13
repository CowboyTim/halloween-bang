#ifndef UART_LOG_DEV_UART0
#define UART_LOG_DEV_UART0 0
#endif

#if UART_LOG_DEV_UART0 == 1
 #define NO_GLOBAL_INSTANCES
 #define NO_GLOBAL_SERIAL
 #include <HardwareSerial.h>
 extern HardwareSerial Serial0;
 extern HardwareSerial Serial1;
 #define UART0         Serial0
 #define Serial        Serial0
 #define UART1         Serial1
#else
 #include <HardwareSerial.h>
 #define UART0         Serial
 #define UART1         Serial1
#endif // UART_LOG_DEV_UART0

#include <stdlib.h>
#include <EdSoftLED.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <driver/rmt.h>
#include <esp_task_wdt.h>

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#define SOUND_VOLUME        20
#define SOUND_1             1
#define SOUND_2             2
#define DFPLAYER_BUSY_PIN   GPIO_NUM_7  // on BANGER
#define DFPLAYER_TX_PIN     GPIO_NUM_12 // on BANGER
#define DFPLAYER_RX_PIN     GPIO_NUM_13 // on BANGER

#define BUILTIN_LED         GPIO_NUM_21 // any, but not working
#define BUTTON_PIN          GPIO_NUM_8  // on BUTTON
#define LED_12V_PIN         GPIO_NUM_7  // on BUTTON

#define WAIT_TIME_BUTTON    3000        // configurable wait time (ms) for button mode - time of animation the banger is showing

#define IS_BUTTON     1
#define IS_BANGER     2
#ifdef BUTTON
uint8_t WHAT = IS_BUTTON;
#endif
#ifdef BANGER
uint8_t WHAT = IS_BANGER;
#endif

#define LINE_START    0
#define LINE_END    111
#define RING_START  112
#define RING_END    299

#define LED_STRIP_GPIO_PIN  GPIO_NUM_4  // data pin to DIN on the strip
#define NUM_LEDS                   300  // can be shorter
EdSoftLED strip(NUM_LEDS, LED_STRIP_GPIO_PIN, WS2812GRB);

HardwareSerial DFPLayerSerial(2);  // Use UART2
DFRobotDFPlayerMini myDFPlayer;

uint8_t animation_is_running = 0;
uint8_t trigger_animation = 0;
unsigned long last_activity = 0;
uint8_t receiverAddress[] = {0xe4, 0xb3, 0x23, 0xf9, 0x43, 0xf0};

#define WATCHDOG_TIMEOUT_MS 60000   // 60s
esp_task_wdt_user_handle_t wdt_user_handle;

#define doYIELD myYIELD()

#define LOG(...)        _log_l(__VA_ARGS__);
#define NOINLINE        __attribute__((noinline,noipa))
#define INLINE          __attribute__((always_inline))
#define ALIGN(x)        __attribute__((aligned(x)))
#define LOG_TIME_FORMAT "[\%H:\%M:\%S][info]: "

#ifdef LOG_VERBOSE
uint8_t verbose = 1;
#else
uint8_t verbose = 0;
#endif

NOINLINE
const char * PT(const char *tformat) {
  ALIGN(4) static char T_buffer[512] = {""};
  static time_t t = 0;
  static struct tm gm_new_tm = {0};
  time(&t);
  if(localtime_r(&t, &gm_new_tm) == NULL) {
    T_buffer[0] = 0;
    return (const char *)&T_buffer;
  }
  memset(T_buffer, 0, sizeof(T_buffer));
  size_t s = strftime(T_buffer, sizeof(T_buffer), tformat, &gm_new_tm);
  if(s == 0)
    T_buffer[0] = 0;
  return (const char *)&T_buffer;
}

NOINLINE
void LOGPRINT(const char *buf){
    // For native USB CDC, check if a host is connected (if supported), 
    // or ensure we don't block indefinitely if the buffer is full.
    #if ARDUINO_USB_CDC_ON_BOOT
      // If CDC is enabled, check if DTR is set (terminal is open) 
      // or write with a timeout so it never freezes the CPU.
      if (!Serial) return; // Skip logging entirely if nobody is listening!
    #endif
    if(!verbose)
        return;
    UART0.write(buf, strlen(buf));
}

NOINLINE
void do_vprintf(uint8_t t, const char *tf, const char *_fmt, va_list args) {
  ALIGN(4) static char obuf[256] = {0};
  if(_fmt == NULL && tf == NULL)
    return;
  if((t & 2) && tf != NULL)
    LOGPRINT(PT(tf));
  if(_fmt == NULL)
    return;
  static int s = 0;
  s = vsnprintf(obuf, sizeof(obuf), _fmt, args);
  if(s < 0)
    obuf[0] = 0;
  else if(s >= sizeof(obuf))
    obuf[sizeof(obuf) - 1] = 0;
  else
    obuf[s] = 0;

  if(t & 1) {
    LOGPRINT(obuf);
    LOGPRINT("\n");
  } else {
    LOGPRINT(obuf);
  }
}

NOINLINE
void do_printf(uint8_t t, const char *tf, const char *_fmt, ...) {
  va_list args;
  va_start(args, _fmt);
  do_vprintf(t, tf, _fmt, args);
  va_end(args);
}

NOINLINE
void _log_l(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  do_vprintf(3, LOG_TIME_FORMAT, fmt, args);
  va_end(args);
}


void setup_cpu_speed(uint32_t freq_mhz = 160) {
  uint32_t xtal_f = getXtalFrequencyMhz();
  uint32_t cpu_f = getCpuFrequencyMhz();
  LOG("[ESP] Current CPU frequency: %d MHz", cpu_f);
  if(xtal_f != 0) {
    LOG("[ESP] Detected XTAL frequency: %d MHz", xtal_f);
    if(xtal_f != 40) {
      LOG("[ESP] Warning: Unusual XTAL frequency detected, expected 40 MHz");
    } else {
      if(xtal_f == 40) {
        // 40 MHz XTAL, set CPU to 80, 160
        // 40 MHz XTAL also allows 10, 20, 40 MHz CPU
        cpu_f = freq_mhz;
        LOG("[ESP] Setting CPU frequency to %d MHz for %d MHz XTAL", cpu_f, xtal_f);
        setCpuFrequencyMhz(cpu_f);
      }
      cpu_f = getCpuFrequencyMhz();
      LOG("[ESP] Current CPU frequency: %d MHz", cpu_f);
    }
  } else {
    LOG("[ESP] Failed to detect XTAL frequency, defaulting to 40 MHz");
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
        LOG("[WDT] Failed to reset WDT: %s", esp_err_to_name(ok));
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

void button_setup(){
  // Register peer
  // for BUTTON: target receiverAddress
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  LOG("button INPUT_PULLUP PIN: %d", BUTTON_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // 12V LED control
  pinMode(LED_12V_PIN, OUTPUT);
  digitalWrite(LED_12V_PIN, HIGH);  // ON at wait time
  // Attach interrupt on FALLING edge (when the button pulls the pin to GND)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_pressed, FALLING);

  // minimal 80MHz, else ESP-NOW doesnt work on esp32s3
  setup_cpu_speed(80);
}

void banger_setup(){
  // Fancy led strip
  setup_led_strip();

  // DFPPlayer serial setup
  doYIELD;
  LOG("DFPlayer Mini WAIT 1");
  delay(1000);
  LOG("DFPlayer UART setup");
  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  LOG("DFPlayer Mini WAIT 2");
  delay(1000);
  LOG("DFPlayer Mini setup START");
  while(!myDFPlayer.begin(DFPLayerSerial, true, true)){
    doYIELD;
    LOG("DFPlayer Mini not detected!");
    delay(500);
  }
  doYIELD;
  LOG("DFPlayer Mini ready!");
  LOG("Will check BUSY pin number: %u", DFPLAYER_BUSY_PIN);
  pinMode(DFPLAYER_BUSY_PIN, INPUT);
  while(digitalRead(DFPLAYER_BUSY_PIN) == LOW){
    doYIELD;
    LOG("DFPlayer BUSY pin still LOW, waiting");
    delay(100);
  }
  LOG("DFPlayer BUSY HIGH, continue");
  myDFPlayer.volume(10);
  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  myDFPlayer.enableDAC();
  LOG("DFPlayer setup complete");

  // for BANGER: just a callback
  esp_now_register_recv_cb(OnDataRecv);
}

void setup() {
  UART0.begin(115200);

  setup_wdt();

  // builtin LED (fails for the EdSoftLED's use of RMT)
  //neopixelWrite(BUILTIN_LED, 0, 0, 200); // (Pin, Green, Red, Blue)

  if (!rmtInit(LED_STRIP_GPIO_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
    LOG("init sender failed");
  }

  // enable WiFi-now
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (esp_now_init() != ESP_OK) {
    LOG("Error initializing ESP-NOW");
    return;
  }
  if(WHAT == IS_BANGER){
     banger_setup();
  }
  if(WHAT == IS_BUTTON){
     button_setup();
  }
}

void do_banger_loop(){
  // BANGER just logs some info, the OnDataRecv handles the real thing
  if(trigger_animation == 1){
    trigger_animation = 0;
    LOG("received DEMOLITION request!");
    if(!animation_is_playing()){
      printed_ready = 0;
      play_something();
    } else {
      LOG("animation already running");
    }
  }
  if(animation_is_playing()){
    if(millis() - last_activity > 100){
      last_activity = millis();
      LOG("DFPlayer is playing (Busy)");
    }
  } else {
    if(!printed_ready){
      printed_ready = 1;
      LOG("READY TO DEMOLISH");
    }
  }
}

void do_button_loop(){
  uint8_t do_send_bang = 0;
  portENTER_CRITICAL(&r_mux);
  if(button_was_pressed){
      button_was_pressed = 0;
      do_send_bang = 1;
  }
  portEXIT_CRITICAL(&r_mux);
  if(do_send_bang){
      digitalWrite(LED_12V_PIN, LOW);
      LOG("ADHD style sending BANG");
      char messaggio[] = "BANG";
      esp_err_t err = esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
      if(err != ESP_OK){
        LOG("problem ESP-NOW send: %s", esp_err_to_name(err));
      } else {
        LOG("BANG sent!");
      }
      // debounce button + wait for animation time
      delay(300);
      // blink LED during wait time, like a buzzer being pushed
      unsigned long wait_start = millis();
      while (millis() - wait_start < WAIT_TIME_BUTTON) {
        digitalWrite(LED_12V_PIN, !digitalRead(LED_12V_PIN));  // toggle LED
        delay(50);  // 50ms toggle = ~60 blinks per 3s
      }
      digitalWrite(LED_12V_PIN, HIGH);
      LOG("WAIT for BUTTON press, LAMP ON");
  } else {
    delay(5);
  }
}

void loop() {
  doYIELD;
  if(WHAT == IS_BANGER){
      do_banger_loop();
  }
  if(WHAT == IS_BUTTON){
      do_button_loop();
  }
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
  myDFPlayer.volume(SOUND_VOLUME); // Volume 0-30
  LOG("Playing File: 1");
  myDFPlayer.play(SOUND_1);

  // ledstrip clear, +1
  run_ledstrip_animation_1();

  while(sound_is_playing()){
    doYIELD;
    delay(1);
  }

  // sound effect (Kabooom)
  myDFPlayer.stop();
  myDFPlayer.volume(SOUND_VOLUME); // Volume 0-30
  LOG("Playing File: 2");
  myDFPlayer.play(SOUND_2);

  // ledstrip +2, clear
  run_ledstrip_animation_2();

  while(sound_is_playing()){
    doYIELD;
    delay(1);
  }

  // done
  LOG("done showing demolition animation");
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
  LOG("RUNNING Animation LED 1");
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
  LOG("RUNNING Animation LED 1: DONE");
}

void run_ledstrip_animation_2() {
  LOG("RUNNING Animation LED 2: Inferno Ring");
  
  // Run for more frames or a slightly faster cadence to increase intensity
  for (int frame = 0; frame < 80; frame++) {
    doYIELD;
    strip.clear();

    // Iterate across the ring LEDs surrounding the target (tail minus 40 LEDs)
    for (int rIdx = RING_START; rIdx <= RING_END - 40; rIdx++) {
      // Create wild, erratic fire variations (deep reds, bright oranges, and sudden white-hot flare-ups)
      int flameColorType = random(0, 10);
      
      if (flameColorType < 3) {
        // Sudden violent white-hot flare - more orange/yellow white
        strip.setPixelColor(rIdx, strip.Color(255, 150, 50));
      } else if (flameColorType < 7) {
        // Intense fiery orange/yellow - increased brightness with green for orange
        int intensity = random(200, 255);
        int green = random(50, 200);
        strip.setPixelColor(rIdx, strip.Color(255, green, 0));
      } else {
        // Deep glowing ember red - with some orange mixed in
        int redIntensity = random(150, 255);
        int green = random(20, 80);
        strip.setPixelColor(rIdx, strip.Color(redIntensity, green, 0));
      }
    }

    strip.show();
    
    // Short delay to keep it blazing fast and frantic
    delay(20); 
  }
  
  strip.clear();
  strip.show();
  LOG("RUNNING Animation LED 2: DONE");
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
    LOG("Failed to initialize task watchdog: %s", esp_err_to_name(ok));
    return;
  }
  LOG("[WDT] Task watchdog initialized");

  // Register watchdog timeout callback
  ok = esp_task_wdt_add_user((char*)"main", &wdt_user_handle);
  if(ok != ESP_OK) {
    LOG("[WDT] Failed to register task: %s", esp_err_to_name(ok));
  } else {
    LOG("[WDT] Watchdog timeout callback registered successfully");
  }
}

void esp_task_wdt_isr_user_handler(void){
  // CRITICAL: Must use abort() not esp_restart() in ISR context!
  // esp_restart() is NOT ISR-safe and will trigger Interrupt WDT
  LOG("[WDT] 1 Task watchdog timeout occurred!");
}



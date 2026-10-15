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
#include "EdSoftLED.h"
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #error "This board is not supported or is missing Wi-Fi hardware!"
#elif defined(ESP32)
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
  #include <driver/gpio.h>
  #include <driver/rmt.h>
  #include <esp_task_wdt.h>
  // ESP32 fallback if needed
#else
  #error "This board is not supported or is missing Wi-Fi hardware!"
#endif

#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#ifdef EXPLOSION
#define SOUND_VOLUME        10  // EXPLOSION box, DFPlayer volume 0-30: lowered while its DFPlayer ran from 3.3 V
#else
#define SOUND_VOLUME        20  // BANGER, DFPlayer volume 0-30
#endif
#define SOUND_1             1
#define SOUND_2             2
#if defined(EXPLOSION) && CONFIG_IDF_TARGET_ESP32C3
// EXPLOSION sound box on a C3 SuperMini (free pins, UART1)
#define DFPLAYER_UART_NR    1
#define DFPLAYER_BUSY_PIN   GPIO_NUM_3  // on EXPLOSION (C3): DFPlayer BUSY
#define DFPLAYER_TX_PIN     GPIO_NUM_5  // on EXPLOSION (C3): to DFPlayer RX
#define DFPLAYER_RX_PIN     GPIO_NUM_6  // on EXPLOSION (C3): from DFPlayer TX
#else
#define DFPLAYER_UART_NR    2
#define DFPLAYER_BUSY_PIN   GPIO_NUM_7  // on BANGER (and EXPLOSION on an S3)
#define DFPLAYER_TX_PIN     GPIO_NUM_12 // on BANGER (and EXPLOSION on an S3)
#define DFPLAYER_RX_PIN     GPIO_NUM_13 // on BANGER (and EXPLOSION on an S3)
#endif

// The light show must run without the DFPlayer (not wired, no SD card, BUSY stuck LOW):
// boot gives up on it after these limits and the show then runs on its own timers.
#define DFPLAYER_INIT_TRIES       3     // begin() attempts at boot (each takes ~2-3 s)
#define DFPLAYER_BUSY_TIMEOUT_MS  3000  // boot: BUSY must go HIGH (idle) within this time
#define SOUND_END_TIMEOUT_MS      2000  // show: wait at most this long for a sound to end after its animation

#define BUILTIN_LED         GPIO_NUM_21 // any, but not working
#define BUTTON_PIN          GPIO_NUM_8  // on BUTTON
#define LED_12V_PIN         GPIO_NUM_7  // on BUTTON

// KILLSWITCH pins, per chip. The unit needs no LED of its own: the board's built-in LED shows
// that a press was seen and sent; the BUTTON's lamp shows the actor what the button is doing.
#if CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3 SuperMini. Not usable: 2/8/9 strapping (8 = built-in LED, 9 = BOOT button),
// 12-17 SPI flash, 18/19 USB, 20/21 UART0. Free: 0, 1, 3, 4, 5, 6, 7, 10.
#define KILL_BUTTON_PIN     GPIO_NUM_3  // external push button to GND (internal pull-up)
#define KILL_BOOT_PIN       GPIO_NUM_9  // on-board BOOT button works too (held while powering up = flash mode)
#define KILL_LED_PIN        GPIO_NUM_8  // built-in blue LED
#define KILL_LED_ON         LOW         // it is lit when the pin is LOW
// Wi-Fi TX power of the C3. Lower power was tried (8.5 dBm); at 7 m the reception was about
// the same at every level, so full power is kept (measured at the BUTTON, 2026-10-15).
#define KILL_TX_POWER       WIFI_POWER_19_5dBm
#else
// ESP32-S3 SuperMini
#define KILL_BUTTON_PIN     GPIO_NUM_8  // push button to GND (internal pull-up)
#define KILL_BOOT_PIN       GPIO_NUM_0  // on-board BOOT button works too
#define KILL_LED_PIN        GPIO_NUM_7  // optional LED, 330 ohm to GND
#define KILL_LED_ON         HIGH
#endif

#define WAIT_TIME_BUTTON    16000       // configurable wait time (ms) for button mode - time of animation the banger is showing (whole show is ~14.5s: 0001.mp3 10.3s + 0002.mp3 4.0s)

#define IS_BUTTON     1
#define IS_BANGER     2
#define IS_KILLSWITCH 3
#define IS_EXPLOSION  4     // sound box only: plays the sounds the BANGER announces (SND1 / SND2)
#if CONFIG_IDF_TARGET_ESP32C3 && (defined(BANGER) || defined(BUTTON))
#error "BANGER and BUTTON pins are for the ESP32-S3 (GPIO 12/13 are flash pins on the C3); only KILLSWITCH has C3 pins"
#endif
#ifdef BUTTON
uint8_t WHAT = IS_BUTTON;
#endif
#ifdef BANGER
uint8_t WHAT = IS_BANGER;
#endif
#ifdef KILLSWITCH
uint8_t WHAT = IS_KILLSWITCH;
#endif
#ifdef EXPLOSION
uint8_t WHAT = IS_EXPLOSION;
#endif

#define LINE_START    0
#define LINE_END    119
#define RING_START  120
#define RING_END    299

#define STROBE_ENABLED      1           // 1 = white strobe LED flashes during the KABOOM, 0 = off (timing: see ANIMATIONS block)
#define STROBE_PIN          GPIO_NUM_5  // on BANGER: gate of the logic-level N-MOSFET that switches the strobe LED (100-220R in series, 10k pull-down to GND)

#define LED_STRIP_GPIO_PIN  GPIO_NUM_4  // data pin to DIN on the strip
#define NUM_LEDS                   300  // can be shorter
EdSoftLED strip(NUM_LEDS, LED_STRIP_GPIO_PIN, WS2812GRB);

HardwareSerial DFPLayerSerial(DFPLAYER_UART_NR);
DFRobotDFPlayerMini myDFPlayer;

uint8_t animation_is_running = 0;
uint8_t trigger_animation = 0;
uint8_t sound_ok = 0;              // BANGER: 1 = DFPlayer found and BUSY works, 0 = lights only
unsigned long last_activity = 0;
#ifndef BANGER_MAC                   // bang.py sets it from boards.toml
#define BANGER_MAC 0xe4, 0xb3, 0x23, 0xf9, 0x43, 0xf0
#endif
uint8_t receiverAddress[] = {BANGER_MAC};
#ifdef EXPLOSION_MAC                 // optional: the BANGER also announces its sounds to this box
uint8_t explosionAddress[] = {EXPLOSION_MAC};
#endif
volatile uint8_t snd_req = 0;        // EXPLOSION: SOUND_1 or SOUND_2 announced by the BANGER

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

// Logging goes to the S3's USB serial (HWCDC) in the normal build. Its write() blocks up to
// ~2 s per call when the PC is plugged in but nothing reads (monitor closed), which stalls
// the show. So: no TX wait (setTxTimeoutMs(0) in setup()), a bigger TX buffer for bursts, one
// write per line, and a line that does not fit is dropped whole. LOG() never blocks.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && UART_LOG_DEV_UART0 == 0
 #define LOG_USB_HWCDC  1
 #define LOG_TX_BUFFER  2048
#else
 #define LOG_USB_HWCDC  0
#endif

NOINLINE
void LOGPRINT(const char *buf, size_t len){
    if(!verbose || len == 0)
        return;
    #if ARDUINO_USB_CDC_ON_BOOT
      if (!Serial) return; // no USB host (e.g. power bank): skip logging
    #endif
    #if LOG_USB_HWCDC
      if (Serial.availableForWrite() < (int)len) return; // PC not reading: drop the line
    #endif
    UART0.write((const uint8_t *)buf, len);
}

NOINLINE
void do_vprintf(uint8_t t, const char *tf, const char *_fmt, va_list args) {
  // timestamp + text + newline in one buffer, written in one go
  ALIGN(4) static char obuf[320] = {0};
  if(_fmt == NULL && tf == NULL)
    return;
  size_t n = 0;
  if((t & 2) && tf != NULL) {
    n = strlcpy(obuf, PT(tf), sizeof(obuf));
    if(n >= sizeof(obuf))
      n = sizeof(obuf) - 1;
  }
  if(_fmt != NULL) {
    int s = vsnprintf(obuf + n, sizeof(obuf) - n, _fmt, args);
    if(s > 0)
      n += ((size_t)s < sizeof(obuf) - 1 - n) ? (size_t)s : sizeof(obuf) - 1 - n;
    if(t & 1) {
      if(n > sizeof(obuf) - 2)
        n = sizeof(obuf) - 2;
      obuf[n++] = '\n';
    }
  }
  obuf[n] = 0;
  LOGPRINT(obuf, n);
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

// ==== MESSAGES BEGIN ====
// ESP-NOW messages (exact match, the payload is not NUL terminated):
//   "BANG"      BUTTON     -> BANGER      start the show
//   "LOCK"      KILLSWITCH -> BUTTON      repeated while the killswitch is held: keep the button locked
//   "FREE"      KILLSWITCH -> BUTTON      killswitch released: unlock now (else the lock times out)
//   "SND1"      BANGER     -> EXPLOSION   the BANGER starts 0001.mp3 (the fuse), play it too
//   "SND2"      BANGER     -> EXPLOSION   the BANGER starts 0002.mp3 (the KABOOM), play it too
volatile uint32_t lock_seen_ms    = 0;   // BUTTON: millis() of the last "LOCK" (0 = none / freed)
uint8_t broadcastAddress[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static bool msg_is(const uint8_t *msg, int len, const char *s) {
  return len == (int)strlen(s) && memcmp(msg, s, len) == 0;
}

void OnDataRecv(const esp_now_recv_info_t *mac, const uint8_t *msg, int len) {
  if(msg_is(msg, len, "BANG")) {
    trigger_animation = 1;
  } else if(msg_is(msg, len, "LOCK")) {
    uint32_t t = millis();
    lock_seen_ms = t ? t : 1;
  } else if(msg_is(msg, len, "FREE")) {
    lock_seen_ms = 0;
  } else if(msg_is(msg, len, "SND1")) {
    snd_req = SOUND_1;
  } else if(msg_is(msg, len, "SND2")) {
    snd_req = SOUND_2;
  }
}
// ==== MESSAGES END ====

// only yields; the watchdog is fed once per loop() pass (wdt_feed), so a wait that never
// ends inside a function is not hidden from it
void myYIELD(){
  yield();
}

void wdt_feed(){
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

// ==== BUTTON / KILLSWITCH BEGIN ====
// -------------------------------------------------------------------------
//  Three units talk over ESP-NOW. LOCK / FREE use broadcast, so the KILLSWITCH needs no MAC
//  address. The BUTTON sends nothing back: its lamp shows the actor what it is doing.
//
//    KILLSWITCH --"LOCK" (while held) / "FREE"-->  BUTTON  --"BANG"-->  BANGER
//
//  The BUTTON always works, except while the actor holds the KILLSWITCH button down (or a
//  switch-style button is closed). While held, the KILLSWITCH repeats "LOCK" every
//  KILL_LOCK_PERIOD_MS and the BUTTON stays LOCKED as long as a LOCK arrived within the last
//  LOCK_TIMEOUT_MS. Release sends "FREE" (KILL_REPEATS times) so it is READY again at once.
//  If those are lost, or the KILLSWITCH resets, goes out of range or runs flat, the lock just
//  times out: a missing message can never keep the button locked. No KILLSWITCH = never locked.
//  A lock also ends by itself after LOCK_MAX_MS, even while still held (a switch left on):
//  the BUTTON then ignores LOCK until the killswitch is released, and the KILLSWITCH stops
//  sending LOCK and blinks fast ("expired, release it").
//
//  Button states:
//    boot -> READY --press--> SHOW --(WAIT_TIME_BUTTON)--> READY (LOCKED if still held)
//    READY --LOCK--> LOCKED --(FREE, no LOCK for LOCK_TIMEOUT_MS or LOCK_MAX_MS; button released)--> READY
//    A LOCK during SHOW does not stop the show (it already runs on the BANGER).
//    Presses in any state except READY are thrown away.
//
//  KILLSWITCH unit: ONE push button or switch. Its built-in LED is on while it is held (the
//  button is being locked), blinks fast once LOCK_MAX_MS has run out, and blinks once at power-up.
// -------------------------------------------------------------------------

#define LOCK_TIMEOUT_MS        1000    // BUTTON: LOCKED until no "LOCK" arrived for this long
#define KILL_LOCK_PERIOD_MS    150     // KILLSWITCH: "LOCK" is repeated this often while held (several may get lost)
#ifndef LOCK_MAX_MS
#define LOCK_MAX_MS            15000   // a lock ends by itself after this long, even if the killswitch stays held
#endif

// BUTTON lamp (12 V via the 2N2222 on LED_12V_PIN, PWM)
#define LAMP_PWM_FREQ          2000    // Hz
#define LAMP_PWM_BITS          8
#define LAMP_LOCKED_LEVEL      0       // 0-255, lamp while locked (0 = dark, a few units = faint ember)
#define LAMP_BREATH_MIN        4       // 0-255, darkest point of the "press me" breathing
#define LAMP_BREATH_PERIOD_MS  3000    // one breath in and out
#define LAMP_SHOW_PERIOD_MS    100     // flicker while the show runs (half on, half off)

// KILLSWITCH / BUTTON sends
#define BANG_REPEATS           3       // BUTTON -> BANGER: "BANG" is sent this many times, the BANGER starts one show only
#define BANG_REPEAT_GAP_MS     60
#define KILL_REPEATS           3       // "FREE" is sent this many times (broadcast has no retries)
#define KILL_REPEAT_GAP_MS     60
#define KILL_BLINK_MS          300     // KILLSWITCH LED: the power-up blink

enum { BS_LOCKED = 0, BS_READY = 2, BS_SHOW = 3 };

// Lamp brightness (0-255) for a button state; ms = time since that state began.
uint8_t lamp_level(uint8_t state, uint32_t ms) {
  switch (state) {
    case BS_LOCKED:
      return LAMP_LOCKED_LEVEL;
    case BS_READY: {
      // slow breathing = "press me"; b*b approximates the eye's brightness curve
      float b = 0.5f - 0.5f * cosf(2.0f * PI * (float)(ms % LAMP_BREATH_PERIOD_MS) / LAMP_BREATH_PERIOD_MS);
      return (uint8_t)(LAMP_BREATH_MIN + (255 - LAMP_BREATH_MIN) * b * b);
    }
    case BS_SHOW:
      return ((ms % LAMP_SHOW_PERIOD_MS) < LAMP_SHOW_PERIOD_MS / 2) ? 255 : 0;
  }
  return 0;
}

// ---- BUTTON unit ---------------------------------------------------------
uint8_t  bstate            = BS_READY;   // works out of the box
uint32_t bstate_since      = 0;
uint32_t button_high_since = 0;     // millis() since the button was last seen released (0 = pressed now)
uint32_t last_lamp_ms      = 0;
uint8_t  bang_repeats_left = 0;     // BANG copies still to send
uint32_t bang_next_ms      = 0;
uint32_t lock_stream_since = 0;     // millis() when the current run of LOCKs began (0 = none)
uint8_t  lock_expired      = 0;     // 1 = this run of LOCKs lasted LOCK_MAX_MS: ignored until released

void button_set_state(uint8_t s) {
  if (s == bstate) return;
  LOG("BUTTON state %d -> %d", bstate, s);
  bstate       = s;
  bstate_since = millis();
}

void button_setup(){
  // peer: the BANGER (gets "BANG")
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  // "LOCK" and "FREE" come from the KILLSWITCH
  esp_now_register_recv_cb(OnDataRecv);

  LOG("button INPUT_PULLUP PIN: %d", BUTTON_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Attach interrupt on FALLING edge (when the button pulls the pin to GND)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_pressed, FALLING);

  // minimal 80MHz, else ESP-NOW doesnt work on esp32s3
  setup_cpu_speed(80);

  // 12V lamp: PWM so it can fade (set up after the CPU speed change)
  ledcAttach(LED_12V_PIN, LAMP_PWM_FREQ, LAMP_PWM_BITS);
  ledcWrite(LED_12V_PIN, lamp_level(bstate, 0));
  bstate_since = millis();
}

void do_button_loop(){
  uint32_t now = millis();

  // Inputs. A queued press is consumed on every pass, so nothing piles up while it is not
  // its turn (a press during LOCKED / SHOW is simply gone).
  uint8_t pressed;
  portENTER_CRITICAL(&r_mux);
  pressed = button_was_pressed;  button_was_pressed = 0;
  portEXIT_CRITICAL(&r_mux);
  // signed: the callback may have stored a millis() a little newer than "now"
  uint32_t lock_ms = lock_seen_ms;
  bool held = lock_ms && (int32_t)(now - lock_ms) < LOCK_TIMEOUT_MS;
  // a lock never lasts longer than LOCK_MAX_MS, however long the killswitch is held
  if (!held) {
    lock_stream_since = 0;
    lock_expired      = 0;
  } else if (!lock_stream_since) {
    lock_stream_since = now ? now : 1;
  } else if (!lock_expired && now - lock_stream_since >= LOCK_MAX_MS) {
    LOG("killswitch held for %d s: lock expired, ignored until it is released", LOCK_MAX_MS / 1000);
    lock_expired = 1;
  }
  if (lock_expired) held = false;

  // button released for at least 50 ms (a button that is held down must be let go first)
  if (digitalRead(BUTTON_PIN) == HIGH) {
    if (!button_high_since) button_high_since = now ? now : 1;
  } else {
    button_high_since = 0;
  }
  bool released = button_high_since && (now - button_high_since >= 50);

  switch (bstate) {
    case BS_LOCKED:
      if (!held && released) {
        // a press has to start while READY, so a press that is still going on does not count
        portENTER_CRITICAL(&r_mux);
        button_was_pressed = 0;
        portEXIT_CRITICAL(&r_mux);
        LOG("killswitch released, button works again");
        button_set_state(BS_READY);
      }
      break;
    case BS_READY:
      if (held) {
        LOG("killswitch held, button locked");
        button_set_state(BS_LOCKED);
      } else if (pressed) {
        LOG("ADHD style sending BANG");
        bang_repeats_left = BANG_REPEATS;   // sent below, a few times in a row
        bang_next_ms      = now;
        button_set_state(BS_SHOW);
      }
      break;
    case BS_SHOW:
      // the show runs on the BANGER; this is just a timer
      if (now - bstate_since >= WAIT_TIME_BUTTON) {
        button_set_state(held ? BS_LOCKED : BS_READY);
      }
      break;
  }

  // Outputs
  // "BANG" goes out BANG_REPEATS times (any one copy is enough, the BANGER ignores the rest)
  if (bang_repeats_left && now >= bang_next_ms) {
    char messaggio[] = "BANG";
    esp_err_t err = esp_now_send(receiverAddress, (uint8_t *)messaggio, strlen(messaggio));
    if(err != ESP_OK){
      LOG("problem ESP-NOW send: %s", esp_err_to_name(err));
    } else {
      LOG("BANG sent! (%d left)", bang_repeats_left - 1);
    }
    bang_repeats_left--;
    bang_next_ms = now + BANG_REPEAT_GAP_MS;
  }
  if (now - last_lamp_ms >= 10) {
    last_lamp_ms = now;
    ledcWrite(LED_12V_PIN, lamp_level(bstate, now - bstate_since));
  }
  delay(2);
}

// ---- KILLSWITCH unit -----------------------------------------------------
uint8_t     ks_stable       = HIGH;   // debounced button level (LOW = held)
uint8_t     ks_raw_last     = HIGH;
uint32_t    ks_raw_since    = 0;
uint32_t    ks_next_lock_ms = 0;      // when the next "LOCK" goes out (while held)
uint8_t     ks_free_left    = 0;      // "FREE" copies still to send (after release)
uint32_t    ks_next_free_ms = 0;
uint32_t    ks_blink_until  = 0;      // power-up blink
uint32_t    ks_held_since   = 0;      // millis() when the current hold began
uint8_t     ks_expired      = 0;      // 1 = held for LOCK_MAX_MS: LOCK stopped until released

void ks_send(const char *m) {
  esp_now_send(broadcastAddress, (const uint8_t *)m, strlen(m));
}

void killswitch_setup(){
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  LOG("killswitch INPUT_PULLUP PINS: %d + BOOT %d, LED PIN: %d", KILL_BUTTON_PIN, KILL_BOOT_PIN, KILL_LED_PIN);
  pinMode(KILL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(KILL_BOOT_PIN, INPUT_PULLUP);
  pinMode(KILL_LED_PIN, OUTPUT);
  digitalWrite(KILL_LED_PIN, !KILL_LED_ON);
  ks_blink_until = millis() + KILL_BLINK_MS;   // one blink at power-up: it is alive

  // minimal 80MHz, else ESP-NOW doesnt work on esp32s3
  setup_cpu_speed(80);
#ifdef KILL_TX_POWER
  WiFi.setTxPower(KILL_TX_POWER);
#endif
  LOG("killswitch Wi-Fi TX power: %d (0.25 dBm units)", (int)WiFi.getTxPower());
}

void do_killswitch_loop(){
  uint32_t now = millis();

  // debounce: the level must stay the same for 30 ms.
  // The external button and the BOOT button do the same (LOW = held).
  uint8_t raw = (digitalRead(KILL_BUTTON_PIN) == LOW || digitalRead(KILL_BOOT_PIN) == LOW) ? LOW : HIGH;
  if (raw != ks_raw_last) {
    ks_raw_last  = raw;
    ks_raw_since = now;
  }
  if (raw != ks_stable && now - ks_raw_since >= 30) {
    ks_stable = raw;
    if (ks_stable == LOW) {                         // held: start locking at once
      LOG("KILLSWITCH held: sending LOCK");
      ks_free_left    = 0;
      ks_next_lock_ms = now;
      ks_held_since   = now;
      ks_expired      = 0;
    } else {                                        // released: unlock now
      LOG("KILLSWITCH released: sending FREE");
      ks_free_left    = KILL_REPEATS;
      ks_next_free_ms = now;
      ks_expired      = 0;
    }
  }
  if (ks_stable == LOW && !ks_expired && now - ks_held_since >= LOCK_MAX_MS) {
    LOG("KILLSWITCH held for %d s: lock expired, sending FREE (release it to lock again)", LOCK_MAX_MS / 1000);
    ks_expired      = 1;
    ks_free_left    = KILL_REPEATS;
    ks_next_free_ms = now;
  }

  if (ks_stable == LOW && !ks_expired && (int32_t)(now - ks_next_lock_ms) >= 0) {
    ks_send("LOCK");
    ks_next_lock_ms = now + KILL_LOCK_PERIOD_MS;
  }
  if (ks_free_left && (int32_t)(now - ks_next_free_ms) >= 0) {
    ks_send("FREE");
    ks_free_left--;
    ks_next_free_ms = now + KILL_REPEAT_GAP_MS;
  }

  // LED: on while held (= the button is being locked), fast blink once that has expired,
  // plus the power-up blink
  uint8_t on = ks_expired ? (now % 200) < 100
                          : (ks_stable == LOW) || (int32_t)(ks_blink_until - now) > 0;
  digitalWrite(KILL_LED_PIN, on ? KILL_LED_ON : !KILL_LED_ON);
  delay(2);
}
// ==== BUTTON / KILLSWITCH END ====

// DFPlayer on the DFPLAYER_* pins, sets sound_ok. Shared by the BANGER and the EXPLOSION box.
void sound_setup(){
  // DFPPlayer serial setup
  doYIELD;
  LOG("DFPlayer Mini WAIT 1");
  delay(3000);
  LOG("DFPlayer UART setup: 9600, %d, %d, %d", SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  DFPLayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  LOG("DFPlayer Mini WAIT 2");
  delay(3000);
  // pull-up: an unconnected BUSY wire reads HIGH = "not playing"
  pinMode(DFPLAYER_BUSY_PIN, INPUT_PULLUP);

  LOG("DFPlayer Mini setup START");
  uint8_t ok = 0;
  for(uint8_t i = 0; i < DFPLAYER_INIT_TRIES && !ok; i++){
    doYIELD;
    ok = myDFPlayer.begin(DFPLayerSerial, true, true);
    if(!ok){
      LOG("DFPlayer Mini not detected (try %d/%d)", i + 1, DFPLAYER_INIT_TRIES);
      delay(500);
    }
  }
  if(ok){
    LOG("DFPlayer Mini ready! Will check BUSY pin number: %u", DFPLAYER_BUSY_PIN);
    uint32_t t0 = millis();
    while(digitalRead(DFPLAYER_BUSY_PIN) == LOW){
      if(millis() - t0 >= DFPLAYER_BUSY_TIMEOUT_MS){
        LOG("DFPlayer BUSY pin stuck LOW");
        ok = 0;
        break;
      }
      doYIELD;
      delay(100);
    }
  }
  sound_ok = ok;
  if(!sound_ok){
    LOG("DFPlayer not working: light show only, no sound");
    return;
  }
  LOG("DFPlayer BUSY HIGH, continue");
  myDFPlayer.volume(SOUND_VOLUME);
  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  myDFPlayer.enableDAC();
  LOG("DFPlayer setup complete");
}

void banger_setup(){
  // first, so a BANG is never missed whatever happens with the DFPlayer below
  esp_now_register_recv_cb(OnDataRecv);

#if STROBE_ENABLED
  // strobe LED off from the start (the 10k pull-down keeps it off while the ESP32 boots)
  pinMode(STROBE_PIN, OUTPUT);
  digitalWrite(STROBE_PIN, LOW);
#endif

  // Fancy led strip
  setup_led_strip();

#ifdef EXPLOSION_MAC
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, explosionAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  LOG("EXPLOSION box: its sounds are announced to it");
#endif

  sound_setup();
}

void explosion_setup(){
  // first, so a SND is never missed whatever happens with the DFPlayer below
  esp_now_register_recv_cb(OnDataRecv);
  sound_setup();
}

// EXPLOSION box: plays the sound the BANGER announces. A repeated SND (the BANGER sends
// each one once, the Wi-Fi retries it) is ignored for SND_DEDUPE_MS after it played.
#define SND_DEDUPE_MS 1000
uint32_t snd_last_ms[3] = {0, 0, 0};

void do_explosion_loop(){
  if(snd_req){
    uint8_t n = snd_req;
    snd_req = 0;
    uint32_t now = millis();
    if(!snd_last_ms[n] || now - snd_last_ms[n] >= SND_DEDUPE_MS){
      snd_last_ms[n] = now ? now : 1;
      play_sound(n);
    }
  }
  delay(2);
}

void setup() {
#if LOG_USB_HWCDC
  Serial.setTxBufferSize(LOG_TX_BUFFER);  // room for the boot burst of log lines
#endif
  UART0.begin(115200);
#if LOG_USB_HWCDC
  Serial.setTxTimeoutMs(0);               // never wait for the PC to read (see LOGPRINT)
#endif

  setup_wdt();

  // builtin LED (fails for the EdSoftLED's use of RMT)
  //neopixelWrite(BUILTIN_LED, 0, 0, 200); // (Pin, Green, Red, Blue)

  // only the BANGER has a strip (GPIO 4 is a free pin on the others)
  if (WHAT == IS_BANGER && !rmtInit(LED_STRIP_GPIO_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) {
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
  if(WHAT == IS_KILLSWITCH){
     killswitch_setup();
  }
  if(WHAT == IS_EXPLOSION){
     explosion_setup();
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
      // copies of the same BANG (and any early press) that arrived during the show are dropped
      trigger_animation = 0;
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

void loop() {
  wdt_feed();
  doYIELD;
  if(WHAT == IS_BANGER){
      do_banger_loop();
  }
  if(WHAT == IS_BUTTON){
      do_button_loop();
  }
  if(WHAT == IS_KILLSWITCH){
      do_killswitch_loop();
  }
  if(WHAT == IS_EXPLOSION){
      do_explosion_loop();
  }
}

uint8_t animation_is_playing() {
  if(animation_is_running)
    return 1;
  return sound_is_playing();
}

uint8_t sound_is_playing(){
  if(!sound_ok)
    return 0;
  int busyState = digitalRead(DFPLAYER_BUSY_PIN);
  if(busyState == LOW)
    return 1;
  return 0;
}

// wait for the sound to end, but never longer than SOUND_END_TIMEOUT_MS: a BUSY pin that
// stays LOW means the DFPlayer is broken, so the next shows run without sound
void wait_sound_end(uint8_t n){
  uint32_t t0 = millis();
  while(sound_is_playing()){
    if(millis() - t0 >= SOUND_END_TIMEOUT_MS){
      LOG("sound %d did not end (BUSY stuck LOW?): light show only from now on", n);
      sound_ok = 0;
      break;
    }
    doYIELD;
    delay(1);
  }
}

void play_sound(uint8_t n){
  if(!sound_ok){
    LOG("no DFPlayer, skipping File: %d", n);
    return;
  }
  myDFPlayer.volume(SOUND_VOLUME); // Volume 0-30
  LOG("Playing File: %d", n);
  myDFPlayer.play(n);
}

// tell the EXPLOSION box (if this build has one) that a sound starts now. Sent whether or not
// the DFPlayer here works. The box is never waited for; check on the bench that a missing box
// does not delay the show (esp_now_send to an absent peer may take time while Wi-Fi retries)
void send_snd(uint8_t n){
#ifdef EXPLOSION_MAC
  const char *m = (n == SOUND_1) ? "SND1" : "SND2";
  esp_now_send(explosionAddress, (const uint8_t *)m, 4);
#endif
}

void play_something() {
  animation_is_running = 1;
  last_activity = millis();

  // sound effect 1 (Whooosh)
  send_snd(SOUND_1);
  play_sound(SOUND_1);

  // ledstrip clear, +1
  run_ledstrip_animation_1();
  wait_sound_end(SOUND_1);

  // sound effect (Kabooom)
  if(sound_ok)
    myDFPlayer.stop();
  send_snd(SOUND_2);
  play_sound(SOUND_2);

  // ledstrip +2, clear
  run_ledstrip_animation_2();
  wait_sound_end(SOUND_2);

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

// ==== ANIMATIONS BEGIN ====
// -------------------------------------------------------------------------
//  TUNING - change these to taste
// -------------------------------------------------------------------------
#define FUSE_LEN          (LINE_END - LINE_START + 1)   // 152 LEDs
#define RING_LEN          (RING_END - RING_START + 1)   // 148 LEDs
#define FUSE_TAIL         14      // length of the glowing trail behind the spark (LEDs)

#define FUSE_BURN_MS      6000    // time for the spark to run the fuse (match SOUND_1 length)
#define FAKEOUT_ENABLED   1       // 1 = spark stalls, nearly dies, re-ignites with a flare
#define FAKEOUT_AT_PCT    55      // where it stalls (% of the way along the fuse)
#define FAKEOUT_MS        1000    // how long the stall lasts

#define RING_IGNITE_MS    1800    // fire spreads both ways around the ring
#define BUILDUP_MIN_MS    2000    // minimum "about to blow" time (also waits for SOUND_1 to end)
#define BUILDUP_MAX_MS    15000   // safety: never wait longer than this for the sound

#define KABOOM_FLASH_MS   180     // white flash (vampire disappears here!)
#define KABOOM_FIRE_MS    600     // flash + fireball fade
#define KABOOM_TOTAL_MS   3800    // whole KABOOM animation incl. embers + purple ghost wisp

// Strobe burst during the KABOOM (STROBE_ENABLED / STROBE_PIN are defined with the pins at the top).
// Runs over the slide-whistle "vanish" of 0002.mp3: the vampire steps out between the flashes.
// Keep it a short burst: flashes at 8-10 Hz are in the photosensitive-epilepsy range -> put up a warning sign.
#define STROBE_START_MS     150   // after the first white flash
#define STROBE_DURATION_MS  1500  // length of the burst
#define STROBE_PERIOD_MS    110   // ~9 flashes per second
#define STROBE_ON_MS        40    // on-time per flash (short = crisp)

// -------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------
float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
float rnd01() { return random(0, 1000) / 1000.0f; }

uint32_t rgbf(float r, float g, float b) {
  return strip.Color((uint8_t)clampf(r, 0, 255), (uint8_t)clampf(g, 0, 255), (uint8_t)clampf(b, 0, 255));
}

// Black-body style fire palette: 0 = black, 0.33 = red, 0.66 = orange/yellow, 1 = white
uint32_t fireColor(float h) {
  h = clampf(h, 0.0f, 1.0f) * 3.0f;
  return rgbf(h * 255.0f, (h - 1.0f) * 255.0f, (h - 2.0f) * 255.0f);
}

// Moment (ms since start) the spark reaches the end of the fuse
float fuse_cross_ms() {
  float te = powf((float)FUSE_LEN / (FUSE_LEN + FUSE_TAIL), 1.0f / 1.25f) * FUSE_BURN_MS;
#if FAKEOUT_ENABLED
  float stallT = powf(FAKEOUT_AT_PCT / 100.0f, 1.0f / 1.25f) * FUSE_BURN_MS;
  if (te >= stallT) te += FAKEOUT_MS;
#endif
  return te;
}

// Spark position in LEDs (can be > FUSE_LEN so the tail runs off the end).
// The spark accelerates along the fuse. stallFrac >= 0 while stalled (0..1), flare = just re-ignited.
float fuse_head_pos(uint32_t ms, float *stallFrac, bool *flare) {
  float te = (float)ms;
  *stallFrac = -1.0f;
  *flare = false;
#if FAKEOUT_ENABLED
  float stallT = powf(FAKEOUT_AT_PCT / 100.0f, 1.0f / 1.25f) * FUSE_BURN_MS;
  if (ms >= stallT) {
    if (ms < stallT + FAKEOUT_MS) {
      te = stallT;
      *stallFrac = (ms - stallT) / (float)FAKEOUT_MS;
    } else {
      te = ms - FAKEOUT_MS;
      if (ms < stallT + FAKEOUT_MS + 350) *flare = true;
    }
  }
#endif
  return powf(clampf(te / FUSE_BURN_MS, 0.0f, 1.0f), 1.25f) * (FUSE_LEN + FUSE_TAIL);
}

// -------------------------------------------------------------------------
//  Fuse: brown twisted rope, white-hot spark, fiery trail, burnt = dark
// -------------------------------------------------------------------------
void draw_fuse(uint32_t ms) {
  float stallFrac; bool flare;
  float head = fuse_head_pos(ms, &stallFrac, &flare);
  int tail = flare ? FUSE_TAIL * 2 : FUSE_TAIL;

  float k = 1.0f;                                   // spark brightness (dies during the fake-out)
  if (stallFrac >= 0.0f)
    k = clampf(1.0f - stallFrac * 1.4f, 0.0f, 1.0f) * (0.4f + 0.6f * rnd01());

  for (int i = 0; i < FUSE_LEN; i++) {
    float d = head - i;
    uint32_t c = 0;
    if (d < 0) {
      c = ((i / 2) % 2) ? rgbf(95, 45, 6) : rgbf(55, 22, 2);      // unburnt rope (twist pattern)
    } else if (d < tail) {
      float h = (1.0f - d / tail) * 0.85f + rnd01() * 0.15f;
      c = fireColor(h * k);
      if (d < 1.5f) c = flare ? rgbf(255, 255, 255) : rgbf(255 * k, 200 * k, 90 * k);
    }
    strip.setPixelColor(LINE_START + i, c);
  }

  // crackling sparks around the head
  if (stallFrac < 0.0f) {
    for (int s = 0; s < 3; s++) {
      int idx = (int)head + random(-6, 7);
      if (idx >= 0 && idx < FUSE_LEN) strip.setPixelColor(LINE_START + idx, rgbf(255, 220, 100));
    }
  } else if (stallFrac > 0.7f && random(8) == 0) {   // last dying ember
    int idx = (int)head;
    if (idx >= 0 && idx < FUSE_LEN) strip.setPixelColor(LINE_START + idx, rgbf(120, 10, 0));
  }
}

// -------------------------------------------------------------------------
//  Ring: dark dynamite until the spark arrives, then fire spreads both ways,
//  flickers faster and whiter ("build-up") until KABOOM
// -------------------------------------------------------------------------
void draw_ring(uint32_t ms, float ringStartMs) {
  if (ms < ringStartMs) {
    for (int j = 0; j < RING_LEN; j++) strip.setPixelColor(RING_START + j, rgbf(60, 5, 0));
    return;
  }
  float rt     = ms - ringStartMs;
  float spread = clampf(rt / RING_IGNITE_MS, 0.0f, 1.0f) * (RING_LEN / 2.0f);
  float build  = clampf((rt - RING_IGNITE_MS) / (float)BUILDUP_MIN_MS, 0.0f, 1.0f);
  float speed  = 0.010f * (1.0f + build * 2.5f);

  for (int j = 0; j < RING_LEN; j++) {
    int d = (j < RING_LEN - j) ? j : (RING_LEN - j);   // distance from where the fuse enters
    uint32_t c;
    if (d <= spread) {
      float h = (0.65f + 0.15f * build) * (0.45f + 0.55f * rnd01())
              + 0.20f * sinf(j * 0.8f - ms * speed)
              + 0.45f * build * build;
      c = fireColor(h);
      if (rt < RING_IGNITE_MS && d > spread - 2) c = rgbf(255, 240, 200);   // white-hot leading edge
    } else {
      c = rgbf(60, 5, 0);
    }
    strip.setPixelColor(RING_START + j, c);
  }
}

// -------------------------------------------------------------------------
//  Animation 1 (during SOUND_1): fuse burns -> ring ignites -> build-up.
//  Does NOT clear at the end: the ring keeps glowing white-hot until KABOOM.
// -------------------------------------------------------------------------
void run_ledstrip_animation_1() {
  LOG("RUNNING Animation LED 1: fuse + ring ignition");
  float ringStart = fuse_cross_ms();
  uint32_t buildDone = (uint32_t)(ringStart + RING_IGNITE_MS + BUILDUP_MIN_MS);
  uint32_t t0 = millis();

  for (;;) {
    doYIELD;
    uint32_t ms = millis() - t0;
    strip.clear();
    draw_fuse(ms);
    draw_ring(ms, ringStart);
    strip.show();

    if (ms >= buildDone && !sound_is_playing()) break;
    if (ms >= buildDone + BUILDUP_MAX_MS) {           // safety net: BUSY stuck LOW, stop trusting it
      LOG("sound 1 did not end: light show only from now on");
      sound_ok = 0;
      break;
    }
    delay(2);
  }
  LOG("RUNNING Animation LED 1: DONE");
}

// -------------------------------------------------------------------------
//  Animation 2 (during SOUND_2): KABOOM flash + shockwave down the fuse,
//  fading fireball, dying embers, purple ghost wisp drifting back along the fuse.
// -------------------------------------------------------------------------
void strobe_update(uint32_t ms) {
#if STROBE_ENABLED
  bool on = ms >= STROBE_START_MS && ms < STROBE_START_MS + STROBE_DURATION_MS
         && ((ms - STROBE_START_MS) % STROBE_PERIOD_MS) < STROBE_ON_MS;
  digitalWrite(STROBE_PIN, on ? HIGH : LOW);
#endif
}

void run_ledstrip_animation_2() {
  LOG("RUNNING Animation LED 2: KABOOM");
  uint32_t t0 = millis();

  for (;;) {
    doYIELD;
    uint32_t ms = millis() - t0;
    if (ms >= KABOOM_TOTAL_MS) break;
    strobe_update(ms);
    strip.clear();

    if (ms < KABOOM_FIRE_MS) {
      // ring: white flash that cools to orange/red
      float cool = (ms < KABOOM_FLASH_MS) ? 1.0f
                 : 1.0f - (ms - KABOOM_FLASH_MS) / (float)(KABOOM_FIRE_MS - KABOOM_FLASH_MS);
      for (int j = 0; j < RING_LEN; j++) {
        float h = (ms < KABOOM_FLASH_MS) ? 1.0f : (0.15f + 0.85f * cool) * (0.4f + 0.6f * rnd01());
        strip.setPixelColor(RING_START + j, fireColor(h));
      }
      // shockwave racing back down the fuse
      float front = ms * 1.2f;                           // LEDs per ms
      for (int i = 0; i < FUSE_LEN; i++) {
        float dist = (FUSE_LEN - 1 - i);
        if (dist < front && dist > front - 25) {
          float b = 1.0f - (front - dist) / 25.0f;
          strip.setPixelColor(LINE_START + i, rgbf(255 * b, 220 * b, 160 * b));
        }
      }
    } else {
      float tC = ms - KABOOM_FIRE_MS;
      float fade = 1.0f - tC / (float)(KABOOM_TOTAL_MS - KABOOM_FIRE_MS);

      // dying embers on the ring
      for (int j = 0; j < RING_LEN; j++) {
        if (random(100) < (int)(4 + 22 * fade)) {
          float e = (0.3f + 0.7f * rnd01()) * fade;
          strip.setPixelColor(RING_START + j, rgbf(255 * e, 40 * e, 0));
        }
      }
      // purple ghost wisp drifting away from the ring along the fuse
      float front = tC * 0.06f;                          // LEDs per ms
      for (int i = 0; i < FUSE_LEN; i++) {
        float dist = (FUSE_LEN - 1 - i);
        if (dist < front) {
          float w = (0.5f + 0.5f * sinf(dist * 0.09f - tC * 0.006f)) * fade;
          strip.setPixelColor(LINE_START + i, rgbf(110 * w, 10 * w, 220 * w));
        }
      }
    }
    strip.show();
    delay(2);
  }

  strobe_update(KABOOM_TOTAL_MS);   // outside the burst window -> strobe off
  strip.clear();
  strip.show();
  LOG("RUNNING Animation LED 2: DONE");
}
// ==== ANIMATIONS END ====

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



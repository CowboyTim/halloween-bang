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

#define WAIT_TIME_BUTTON    16000       // configurable wait time (ms) for button mode - time of animation the banger is showing (whole show is ~14.5s: 0001.mp3 10.3s + 0002.mp3 4.0s)

#define IS_BUTTON     1
#define IS_BANGER     2
#define IS_KILLSWITCH 3
#ifdef BUTTON
uint8_t WHAT = IS_BUTTON;
#endif
#ifdef BANGER
uint8_t WHAT = IS_BANGER;
#endif
#ifdef KILLSWITCH
uint8_t WHAT = IS_KILLSWITCH;
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

// ==== MESSAGES BEGIN ====
// ESP-NOW messages (exact match, the payload is not NUL terminated):
//   "BANG"      BUTTON     -> BANGER      start the show
//   "ARM"       KILLSWITCH -> BUTTON      start the arming countdown
//   "LOCK"      KILLSWITCH -> BUTTON      lock the button again
//   "S0".."S3"  BUTTON     -> KILLSWITCH  button state (LOCKED, COUNTDOWN, READY, SHOW)
volatile uint8_t  arm_requested   = 0;   // BUTTON: "ARM" received
volatile uint8_t  lock_requested  = 0;   // BUTTON: "LOCK" received
volatile uint8_t  remote_state    = 0;   // KILLSWITCH: last state reported by the BUTTON
volatile uint32_t remote_state_ms = 0;   // KILLSWITCH: millis() of that report (0 = never)
uint8_t broadcastAddress[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static bool msg_is(const uint8_t *msg, int len, const char *s) {
  return len == (int)strlen(s) && memcmp(msg, s, len) == 0;
}

void OnDataRecv(const esp_now_recv_info_t *mac, const uint8_t *msg, int len) {
  if(msg_is(msg, len, "BANG")) {
    trigger_animation = 1;
  } else if(msg_is(msg, len, "ARM")) {
    arm_requested = 1;
  } else if(msg_is(msg, len, "LOCK")) {
    lock_requested = 1;
  } else if(len == 2 && msg[0] == 'S' && msg[1] >= '0' && msg[1] <= '3') {
    remote_state = msg[1] - '0';
    remote_state_ms = millis();
    if(!remote_state_ms) remote_state_ms = 1;
  }
}
// ==== MESSAGES END ====

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

// ==== BUTTON / KILLSWITCH BEGIN ====
// -------------------------------------------------------------------------
//  Three units talk over ESP-NOW. ARM / LOCK / status use broadcast, so there are no extra
//  MAC addresses to configure:
//
//    KILLSWITCH --"ARM" / "LOCK"-->  BUTTON  --"BANG"-->  BANGER
//    KILLSWITCH <--"S0".."S3"-----   BUTTON  (its state, repeated every second)
//
//  The KILLSWITCH is OPTIONAL. Without one (or until its button is first pressed) the BUTTON
//  just works: it boots READY and is READY again after every show.
//  The first LOCK from a KILLSWITCH "guards" the button: from then on it only works after the
//  actor ARMs it (and goes back to LOCKED after every show), so kids cannot start the show
//  while the actor is still busy. A reboot of the BUTTON unit forgets the guard (back to READY).
//
//  Button states:
//    boot -> READY --press--> SHOW --(WAIT_TIME_BUTTON)--> READY          (not guarded)
//    LOCK (from anywhere, also during SHOW) -> guarded; READY / COUNTDOWN -> LOCKED
//    guarded: LOCKED --ARM--> COUNTDOWN --(ARM_DELAY_MS)--> READY --press--> SHOW --> LOCKED
//    after a show the lock is only a cooldown: COOLDOWN_AFTER_SHOW_MS later (30 s) the button
//    is READY again by itself, so a lost ARM never leaves it dead. A LOCK from the actor
//    (during or after the show) holds the button locked until the next ARM instead.
//    Presses in any state except READY are thrown away.
//
//  KILLSWITCH unit: ONE push button. Hold it ARM_HOLD_MS = ARM, a short tap = LOCK (so an
//  accidental tap locks, it can never arm). Its LED shows what the BUTTON is doing.
// -------------------------------------------------------------------------

#define ARM_DELAY_MS           10000   // after ARM: this long until the button works (0 = at once)
#define COOLDOWN_AFTER_SHOW_MS 30000   // guarded: this long after a show the button unlocks by itself (an ARM may have been lost; 0 = stay locked until ARM)
#define AUTO_REARM_AFTER_SHOW  0       // 1 = a new countdown starts by itself after each show, 0 = stay locked until re-armed

// BUTTON lamp (12 V via the 2N2222 on LED_12V_PIN, PWM)
#define LAMP_PWM_FREQ          2000    // Hz
#define LAMP_PWM_BITS          8
#define LAMP_LOCKED_LEVEL      0       // 0-255, lamp while locked (0 = dark, a few units = faint ember)
#define LAMP_BREATH_MIN        4       // 0-255, darkest point of the "press me" breathing
#define LAMP_BREATH_PERIOD_MS  3000    // one breath in and out
#define LAMP_SHOW_PERIOD_MS    100     // flicker while the show runs (half on, half off)

#define STATUS_PERIOD_MS       1000    // BUTTON repeats its state at least this often
#define STATUS_TIMEOUT_MS      3500    // KILLSWITCH: no state for this long = "no link"

// KILLSWITCH unit
#define ARM_HOLD_MS            1000    // hold this long to ARM, a shorter tap sends LOCK
#define BANG_REPEATS           3       // BUTTON -> BANGER: "BANG" is sent this many times, the BANGER starts one show only
#define BANG_REPEAT_GAP_MS     60
#define KILL_REPEATS           3       // every command is sent this many times (broadcast has no retries)
#define KILL_REPEAT_GAP_MS     60
#define KILL_LED_PIN           GPIO_NUM_7   // on KILLSWITCH: status LED, 330 ohm to GND

enum { BS_LOCKED = 0, BS_COUNTDOWN = 1, BS_READY = 2, BS_SHOW = 3 };
#define STATE_NO_LINK          99

static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Lamp brightness (0-255) for a button state; ms = time since that state began.
uint8_t lamp_level(uint8_t state, uint32_t ms) {
  switch (state) {
    case BS_LOCKED:
      return LAMP_LOCKED_LEVEL;
    case BS_COUNTDOWN: {
      // "waking up": random flicker that gets brighter towards the end of the countdown
      float prog  = ARM_DELAY_MS ? clamp01((float)ms / ARM_DELAY_MS) : 1.0f;
      uint32_t h  = (ms / 70) * 2654435761u;                  // a new random value every 70 ms
      float flick = 0.2f + 0.8f * ((h >> 24) / 255.0f);
      float base  = 0.2f + 0.6f * prog;
      return (uint8_t)(255.0f * base * flick);
    }
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
uint8_t  bstate            = BS_READY;   // works out of the box; a KILLSWITCH LOCK guards it
uint8_t  hold_lock         = 0;          // 1 = the actor locked it: no automatic unlock, only ARM
uint8_t  guarded           = 0;          // 1 after the first LOCK: needs ARM before every show
uint32_t bstate_since      = 0;
uint32_t button_high_since = 0;     // millis() since the button was last seen released (0 = pressed now)
uint32_t last_lamp_ms      = 0;
uint32_t last_status_ms    = 0;
uint8_t  status_dirty      = 1;
uint8_t  bang_repeats_left = 0;     // BANG copies still to send
uint32_t bang_next_ms      = 0;

void button_set_state(uint8_t s) {
  if (s == bstate) return;
  LOG("BUTTON state %d -> %d", bstate, s);
  bstate       = s;
  bstate_since = millis();
  status_dirty = 1;
}

void send_status() {
  char msg[2] = {'S', (char)('0' + bstate)};
  esp_now_send(broadcastAddress, (uint8_t *)msg, 2);
  last_status_ms = millis();
  status_dirty   = 0;
}

void button_setup(){
  // peers: the BANGER (gets "BANG") and broadcast (state for the KILLSWITCH)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  esp_now_add_peer(&peerInfo);
  // "ARM" and "LOCK" come from the KILLSWITCH
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

  // Inputs. Everything queued is consumed on every pass, so nothing piles up while it is not
  // its turn (a press during LOCKED / COUNTDOWN / SHOW is simply gone).
  uint8_t pressed, arm, lock;
  portENTER_CRITICAL(&r_mux);
  pressed = button_was_pressed;  button_was_pressed = 0;
  arm     = arm_requested;       arm_requested      = 0;
  lock    = lock_requested;      lock_requested     = 0;
  portEXIT_CRITICAL(&r_mux);

  // button released for at least 50 ms (a button that is held down must be let go first)
  if (digitalRead(BUTTON_PIN) == HIGH) {
    if (!button_high_since) button_high_since = now ? now : 1;
  } else {
    button_high_since = 0;
  }
  bool released = button_high_since && (now - button_high_since >= 50);

  if (lock) {
    if (!guarded) LOG("BUTTON now guarded by the KILLSWITCH");
    guarded   = 1;
    hold_lock = 1;     // an explicit LOCK holds until the actor ARMs
  }

  switch (bstate) {
    case BS_LOCKED:
      if (arm) {
        hold_lock = 0;
        button_set_state(BS_COUNTDOWN);
      } else if (!hold_lock && COOLDOWN_AFTER_SHOW_MS && now - bstate_since >= COOLDOWN_AFTER_SHOW_MS) {
        LOG("cooldown over, unlocking by itself");
        button_set_state(BS_READY);
      }
      break;
    case BS_COUNTDOWN:
      if (lock) {
        button_set_state(BS_LOCKED);
      } else if (now - bstate_since >= ARM_DELAY_MS && released) {
        // a press has to start while READY, so a press that is still going on does not count
        portENTER_CRITICAL(&r_mux);
        button_was_pressed = 0;
        portEXIT_CRITICAL(&r_mux);
        button_set_state(BS_READY);
      }
      break;
    case BS_READY:
      if (lock) {
        button_set_state(BS_LOCKED);   // (arm is ignored: nothing to arm)
      } else if (pressed) {
        LOG("ADHD style sending BANG");
        bang_repeats_left = BANG_REPEATS;   // sent below, a few times in a row
        bang_next_ms      = now;
        button_set_state(BS_SHOW);
      }
      break;
    case BS_SHOW:
      // the show runs on the BANGER; this is just a timer (ARM is ignored meanwhile, a LOCK is remembered)
      if (now - bstate_since >= WAIT_TIME_BUTTON) {
        if (!guarded)                button_set_state(BS_READY);
        else if (AUTO_REARM_AFTER_SHOW && !hold_lock) button_set_state(BS_COUNTDOWN);
        else                         button_set_state(BS_LOCKED);
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
  if (status_dirty || now - last_status_ms >= STATUS_PERIOD_MS) {
    send_status();
  }
  delay(2);
}

// ---- KILLSWITCH unit -----------------------------------------------------
uint8_t     ks_stable       = HIGH;   // debounced button level (LOW = pressed)
uint8_t     ks_raw_last     = HIGH;
uint32_t    ks_raw_since    = 0;
uint32_t    ks_pressed_since = 0;     // millis() when the current press began
uint8_t     ks_hold_sent    = 0;      // ARM already sent for this press
const char *ks_msg          = NULL;   // command being repeated
uint8_t     ks_repeats_left = 0;
uint32_t    ks_next_send_ms = 0;

void ks_queue(const char *m) {
  ks_msg          = m;
  ks_repeats_left = KILL_REPEATS;
  ks_next_send_ms = millis();
  LOG("KILLSWITCH sends %s", m);
}

// LED pattern for the state the BUTTON reports (on = 1)
uint8_t killswitch_led_on(uint8_t state, uint32_t ms) {
  switch (state) {
    case BS_LOCKED:    return (ms % 2000) < 100;                  // one short blink every 2 s
    case BS_COUNTDOWN: return (ms % 250) < 125;                   // fast blink: counting down
    case BS_READY:     return 1;                                  // solid: the button is live
    case BS_SHOW:      return (ms % 100) < 50;                    // flicker: the show is running
  }
  uint32_t p = ms % 2000;                                         // anything else = no link: two quick blinks
  return (p < 80) || (p >= 160 && p < 240);
}

void killswitch_setup(){
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  // the BUTTON reports its state
  esp_now_register_recv_cb(OnDataRecv);

  LOG("killswitch INPUT_PULLUP PIN: %d, LED PIN: %d", BUTTON_PIN, KILL_LED_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(KILL_LED_PIN, OUTPUT);
  digitalWrite(KILL_LED_PIN, LOW);

  // minimal 80MHz, else ESP-NOW doesnt work on esp32s3
  setup_cpu_speed(80);
}

void do_killswitch_loop(){
  uint32_t now = millis();

  // debounce: the level must stay the same for 30 ms
  uint8_t raw = digitalRead(BUTTON_PIN);
  if (raw != ks_raw_last) {
    ks_raw_last  = raw;
    ks_raw_since = now;
  }
  if (raw != ks_stable && now - ks_raw_since >= 30) {
    ks_stable = raw;
    if (ks_stable == LOW) {                         // pressed
      ks_pressed_since = now ? now : 1;
      ks_hold_sent     = 0;
    } else {                                        // released
      if (!ks_hold_sent) ks_queue("LOCK");          // a tap = LOCK
      ks_pressed_since = 0;
    }
  }
  if (ks_stable == LOW && !ks_hold_sent && now - ks_pressed_since >= ARM_HOLD_MS) {
    ks_hold_sent = 1;                               // held long enough = ARM
    ks_queue("ARM");
  }

  // send the queued command a few times in a row
  if (ks_repeats_left && now >= ks_next_send_ms) {
    esp_now_send(broadcastAddress, (const uint8_t *)ks_msg, strlen(ks_msg));
    ks_repeats_left--;
    ks_next_send_ms = now + KILL_REPEAT_GAP_MS;
  }

  // LED: on while the button is held, otherwise the state the BUTTON reported
  uint8_t state = (remote_state_ms && now - remote_state_ms < STATUS_TIMEOUT_MS) ? remote_state : STATE_NO_LINK;
  digitalWrite(KILL_LED_PIN, (ks_stable == LOW) ? 1 : killswitch_led_on(state, now));
  delay(2);
}
// ==== BUTTON / KILLSWITCH END ====

void banger_setup(){
#if STROBE_ENABLED
  // strobe LED off from the start (the 10k pull-down keeps it off while the ESP32 boots)
  pinMode(STROBE_PIN, OUTPUT);
  digitalWrite(STROBE_PIN, LOW);
#endif

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
  if(WHAT == IS_KILLSWITCH){
     killswitch_setup();
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
    if (ms >= buildDone + BUILDUP_MAX_MS) break;      // safety net
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



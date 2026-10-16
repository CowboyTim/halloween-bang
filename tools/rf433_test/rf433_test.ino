// 433 MHz ASK link range test (STX882 / SRX882, or any raw ASK pair).
// NOT part of the show firmware: a bench test to see whether 433 MHz reaches the BUTTON better
// than ESP-NOW does (the KILLSWITCH link: 10-68% at 7 m).
//
// One sketch, two builds:
//   -DRF_TX  on the ESP32-C3: sends a numbered packet every SEND_PERIOD_MS (the KILLSWITCH's LOCK rate)
//   -DRF_RX  on the ESP32-S3: counts what arrives and prints the result every WINDOW_MS
//
// Wiring (the same pins on both chips, RadioHead sets all three of them):
//   transmitter (C3): STX882 DATA -> GPIO 4, VCC -> 3.3 V first (see README), GND
//   receiver    (S3): SRX882 DATA -> 1 kohm -> GPIO 6, VCC -> 5 V, GND   (its DATA swings at its supply)
//   GPIO 7 is RadioHead's PTT pin: leave it unconnected.
// Antennas: a straight wire of about 17 cm on each module's ANT pad, away from the ESP and metal.
//
// Build and flash (see tools/rf433_test/README.md):
//   arduino-cli compile -b esp32:esp32:esp32c3 --build-property build.extra_flags="-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DESP32 -DRF_TX" tools/rf433_test
#include <RH_ASK.h>
#include <SPI.h>

#if !defined(RF_TX) && !defined(RF_RX)
#error "define RF_TX (transmitter, C3) or RF_RX (receiver, S3)"
#endif

#define RF_BITRATE      2000    // bits per second on the air
#define RF_TX_PIN       4
#define RF_RX_PIN       6
#define RF_PTT_PIN      7
#define SEND_PERIOD_MS  150     // one packet takes about 70 ms on the air at 2000 bit/s
#define WINDOW_MS       4000    // the receiver reports this often

RH_ASK rf(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, RF_PTT_PIN);

void setup() {
  Serial.begin(115200);
  delay(1500);
#ifdef RF_TX
  Serial.println("[RF] transmitter, DATA on GPIO 4");
#else
  Serial.println("[RF] receiver, DATA on GPIO 6");
#endif
  if (!rf.init()) {
    Serial.println("[RF] init FAILED");
  } else {
    Serial.printf("[RF] ready, %d bit/s, packet every %d ms\n", RF_BITRATE, SEND_PERIOD_MS);
  }
}

#ifdef RF_TX
uint16_t seq = 0;
uint32_t next_ms = 0;

void loop() {
  uint32_t now = millis();
  if ((int32_t)(now - next_ms) >= 0) {
    next_ms = now + SEND_PERIOD_MS;
    uint8_t p[4] = {'R', 'T', (uint8_t)(seq >> 8), (uint8_t)seq};
    rf.send(p, sizeof(p));
    rf.waitPacketSent();                       // blocks for the air time of the packet
    if (seq % 25 == 0) Serial.printf("[TX] sent %u\n", seq);
    seq++;
  }
}
#endif

#ifdef RF_RX
uint32_t win_start = 0;
bool     any       = false;
uint16_t first_seq = 0, last_seq = 0, got = 0, other = 0;
uint32_t bad_prev  = 0;

void loop() {
  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  if (rf.recv(buf, &len)) {
    if (len == 4 && buf[0] == 'R' && buf[1] == 'T') {
      uint16_t s = ((uint16_t)buf[2] << 8) | buf[3];
      if (!any) { first_seq = s; any = true; }
      last_seq = s;
      got++;
    } else {
      other++;
    }
  }
  uint32_t now = millis();
  if (now - win_start >= WINDOW_MS) {
    win_start = now;
    uint32_t bad = rf.rxBad();
    uint32_t sent_in_window = WINDOW_MS / SEND_PERIOD_MS;
    if (any) {
      uint16_t expected = (uint16_t)(last_seq - first_seq + 1);    // from the sequence numbers
      Serial.printf("[RX] %2u received of about %u sent (seq %u..%u = %u), %u%%, bad CRC %u, other %u\n",
                    got, sent_in_window, first_seq, last_seq, expected,
                    got * 100 / sent_in_window, (unsigned)(bad - bad_prev), other);
    } else {
      Serial.printf("[RX] nothing valid, bad CRC %u (noise or too weak)\n", (unsigned)(bad - bad_prev));
    }
    bad_prev = bad; any = false; got = 0; other = 0;
  }
}
#endif

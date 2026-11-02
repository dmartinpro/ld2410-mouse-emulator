// LD2410B presence -> USB HID mouse jiggle (ESP32-S3 native USB).
#include "USB.h"
#include "USBHIDMouse.h"

#define JIGGLE_MS  30000UL  // interval between jiggles while present
#define TIMEOUT_MS 5000UL   // no valid frame for this long = absent
#define CORNER_MS  3600000UL   // while present, push the pointer to the top-left corner this often (1 h)
#define REBOOT_MS  86400000UL  // restart the ESP32 this often (24 h)

USBHIDMouse Mouse;
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
uint8_t d[64];  // bytes after the header: len(2), data, footer(4)
uint8_t n = 0;  // bytes received so far, header included
bool present = false;
uint32_t lastFrame = 0, lastMove = 0, lastReset = 0, lastCorner = 0;
bool booted = false;  // true once the first corner push (after USB enumeration) is done

// A HID mouse is relative only: slam the pointer into the top-left corner, the OS clamps it at 0,0.
// ponytail: 60 x 127 px is enough up to about 7600 px wide; raise the count for bigger screens.
void cornerPointer() {
  for (int i = 0; i < 60; i++) { Mouse.move(-127, -127); delay(5); }
}

// Silent radar (e.g. left in config mode by the phone app): enable config, then restart it.
void restartRadar() {
  static const uint8_t en[] = {0xFD, 0xFC, 0xFB, 0xFA, 4, 0, 0xFF, 0, 1, 0, 4, 3, 2, 1};
  static const uint8_t rs[] = {0xFD, 0xFC, 0xFB, 0xFA, 2, 0, 0xA3, 0, 4, 3, 2, 1};
  Serial1.write(en, sizeof en);
  delay(100);
  Serial1.write(rs, sizeof rs);
}

void setup() {
  Serial1.begin(256000, SERIAL_8N1, 18, 17);  // RX=18 (sensor TX), TX=17
  Mouse.begin();
  USB.begin();
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (n < 4) { n = (b == HDR[n]) ? n + 1 : (b == HDR[0]); continue; }
    d[n++ - 4] = b;
    if (n < 6) continue;
    uint16_t len = d[0] | d[1] << 8;
    if (len < 3 || len > 40) { n = 0; continue; }
    if (n < 10 + len) continue;
    // full frame: data = d[2..], footer = d[2+len..]
    if (d[2 + len] == 0xF8 && d[2 + len + 3] == 0xF5 && (d[2] == 1 || d[2] == 2) && d[3] == 0xAA) {
      present = d[4] != 0;
      lastFrame = millis();
    }
    n = 0;
  }
  uint32_t now = millis();
  if (now >= REBOOT_MS) ESP.restart();
  if (!booted && now > 3000) { cornerPointer(); booted = true; lastCorner = now; }  // wait for USB enumeration
  if (now - lastFrame > TIMEOUT_MS) {
    present = false;
    if (now - lastReset > 10000UL) { restartRadar(); lastReset = now; }  // retry every 10 s
  }
  static int8_t led = -1;  // board RGB LED: green = present, red = radar silent, off = absent
  int8_t want = present ? 1 : (now - lastFrame > TIMEOUT_MS ? 2 : 0);
  if (want != led) { led = want; rgbLedWrite(RGB_BUILTIN, want == 2 ? 16 : 0, want == 1 ? 16 : 0, 0); }
  if (!present) lastMove = now;
  else if (now - lastCorner >= CORNER_MS) { cornerPointer(); lastCorner = lastMove = now; }
  else if (now - lastMove >= JIGGLE_MS) {
    Mouse.move(1, 0);
    delay(20);
    Mouse.move(-1, 0);
    lastMove = now;
  }
}

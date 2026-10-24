// LD2410B presence -> USB HID mouse jiggle (ESP32-S3 native USB).
#include "USB.h"
#include "USBHIDMouse.h"

#define JIGGLE_MS  30000UL  // interval between jiggles while present
#define TIMEOUT_MS 5000UL   // no valid frame for this long = absent

USBHIDMouse Mouse;
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
uint8_t d[64];  // bytes after the header: len(2), data, footer(4)
uint8_t n = 0;  // bytes received so far, header included
bool present = false;
uint32_t lastFrame = 0, lastMove = 0;

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
  if (now - lastFrame > TIMEOUT_MS) present = false;
  if (!present) lastMove = now;
  else if (now - lastMove >= JIGGLE_MS) {
    Mouse.move(1, 0);
    delay(20);
    Mouse.move(-1, 0);
    lastMove = now;
  }
}

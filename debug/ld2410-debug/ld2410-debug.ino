// LD2410B debug: parse frames on Serial1, print a status block on Serial once per second.
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
uint8_t d[64];  // bytes after the header: len(2), data, footer(4)
uint8_t n = 0;  // bytes received so far, header included
uint8_t st = 0, mE = 0, sE = 0;
uint16_t mD = 0, sD = 0, dD = 0;
uint32_t frames = 0, bytes = 0, lastFrame = 0, lastPrint = 0, f0 = 0, b0 = 0, lastReset = 0;

// Silent radar (e.g. left in config mode by the phone app): enable config, then restart it.
void restartRadar() {
  static const uint8_t en[] = {0xFD, 0xFC, 0xFB, 0xFA, 4, 0, 0xFF, 0, 1, 0, 4, 3, 2, 1};
  static const uint8_t rs[] = {0xFD, 0xFC, 0xFB, 0xFA, 2, 0, 0xA3, 0, 4, 3, 2, 1};
  Serial1.write(en, sizeof en);
  delay(100);
  Serial1.write(rs, sizeof rs);
}
bool got = false;

void setup() {
  Serial.begin(115200);
  Serial1.begin(256000, SERIAL_8N1, 18, 17);  // RX=18 (sensor TX), TX=17
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    bytes++;
    if (n < 4) { n = (b == HDR[n]) ? n + 1 : (b == HDR[0]); continue; }
    d[n++ - 4] = b;
    if (n < 6) continue;
    uint16_t len = d[0] | d[1] << 8;
    if (len < 11 || len > 40) { n = 0; continue; }
    if (n < 10 + len) continue;
    // full frame: data = d[2..], footer = d[2+len..]
    if (d[2 + len] == 0xF8 && d[2 + len + 3] == 0xF5 && (d[2] == 1 || d[2] == 2) && d[3] == 0xAA) {
      st = d[4];
      mD = d[5] | d[6] << 8;  mE = d[7];
      sD = d[8] | d[9] << 8;  sE = d[10];
      dD = d[11] | d[12] << 8;
      frames++; got = true; lastFrame = millis();
    }
    n = 0;
  }
  uint32_t now = millis();
  if (now - lastFrame > 5000 && now - lastReset > 10000) { restartRadar(); lastReset = now; Serial.print("restart sent\r\n"); }
  if (now - lastPrint < 1000) return;
  float dt = (now - lastPrint) / 1000.0f;
  lastPrint = now;
  bool live = got && now - lastFrame <= 3000;
  uint16_t cm = dD ? dD : (mD ? mD : sD);
  Serial.printf("%s | %.2f m | mov %.2fm e%u | sta %.2fm e%u | %.0f fps\r\n", !live ? "NO FRAME" : st ? "PRESENCE" : "--------",
                cm / 100.0f, mD / 100.0f, mE, sD / 100.0f, sE, (frames - f0) / dt);
  f0 = frames; b0 = bytes;
}

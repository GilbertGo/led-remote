// LED 패널 시계·날씨 표시기 (ESP32)
// - 와이파이로 인터넷 시간과 바깥 날씨(Open-Meteo, 무료·키 없음)를 받아서
// - 블루투스로 LED 패널(LED_BLE_...)에 번갈아 보냄: 시계(패널 자체 시계) → 날씨(패널 글자 명령)
// - 패널은 한 번에 한 기기만 연결되므로, 보낼 때만 잠깐 연결하고 바로 끊음 (그 사이 폰으로 연결 가능)
// 명령 형식은 저장소의 CLAUDE.md 참고 (웹페이지 index.html과 같은 방식)

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <time.h>
#include "secrets.h" // WIFI_SSID, WIFI_PASS (secrets.example.h를 복사해서 만들기)

// ---------- 설정 ----------
const char *PANEL_PREFIX = "LED_BLE";      // 패널 블루투스 이름 앞부분
const float LAT = 37.4563, LON = 126.7052; // 날씨 위치 (인천시청)
const int CLOCK_SECONDS = 30;              // 시계 보여주는 시간
const int WEATHER_SECONDS = 10;            // 날씨 보여주는 시간
const int WEATHER_UPDATE_MINUTES = 10;     // 날씨 새로 받는 간격
const uint8_t CLOCK_STYLE = 1;             // 패널 시계 모양 (0~8)
const bool CLOCK_SHOW_DATE = true;
const int W = 96, H = 16;                  // 패널 크기 (확정)

// ---------- 글자 모양 (웹페이지 글자 그리기로 만든 16줄 점 그림, 왼쪽 칸 = 가장 낮은 비트) ----------
struct Glyph { char c; uint8_t w; uint16_t rows[16]; };
const Glyph GLYPHS[] = {
  { '0', 8, { 0x0000, 0x003c, 0x0066, 0x00c6, 0x00c6, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00c6, 0x0066, 0x003c, 0x0000, 0x0000, 0x0000 } },
  { '1', 8, { 0x0000, 0x0030, 0x003c, 0x003e, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0000, 0x0000, 0x0000 } },
  { '2', 8, { 0x0000, 0x003e, 0x0062, 0x0060, 0x0060, 0x0060, 0x0060, 0x0030, 0x0018, 0x000e, 0x0006, 0x0007, 0x00ff, 0x0000, 0x0000, 0x0000 } },
  { '3', 8, { 0x0000, 0x003c, 0x0062, 0x0060, 0x0060, 0x0060, 0x001c, 0x0060, 0x00e0, 0x00c0, 0x0060, 0x0062, 0x003e, 0x0000, 0x0000, 0x0000 } },
  { '4', 8, { 0x0000, 0x0070, 0x0070, 0x0078, 0x0078, 0x006c, 0x006c, 0x0066, 0x0063, 0x00ff, 0x0060, 0x0060, 0x0060, 0x0000, 0x0000, 0x0000 } },
  { '5', 8, { 0x0000, 0x007e, 0x0006, 0x0006, 0x0006, 0x0006, 0x003e, 0x0060, 0x00c0, 0x00c0, 0x00e0, 0x0062, 0x003e, 0x0000, 0x0000, 0x0000 } },
  { '6', 8, { 0x0000, 0x0078, 0x004c, 0x0006, 0x0006, 0x0006, 0x007f, 0x00e7, 0x00c7, 0x00c6, 0x00c6, 0x0066, 0x003c, 0x0000, 0x0000, 0x0000 } },
  { '7', 8, { 0x0000, 0x00ff, 0x0060, 0x0060, 0x0060, 0x0030, 0x0030, 0x0018, 0x0018, 0x0018, 0x000c, 0x000c, 0x000c, 0x0000, 0x0000, 0x0000 } },
  { '8', 8, { 0x0000, 0x003c, 0x0066, 0x00c6, 0x00c6, 0x0066, 0x003c, 0x0066, 0x00c3, 0x00c3, 0x00c3, 0x00e6, 0x003c, 0x0000, 0x0000, 0x0000 } },
  { '9', 8, { 0x0000, 0x003c, 0x0066, 0x00c7, 0x00c3, 0x00c3, 0x00e6, 0x00fc, 0x00c0, 0x00c0, 0x0060, 0x0072, 0x003e, 0x0000, 0x0000, 0x0000 } },
  { '-', 8, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x007c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 } },
  { '.', 8, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0018, 0x0018, 0x0000, 0x0000, 0x0000 } },
  { 'd', 8, { 0x0000, 0x003c, 0x0064, 0x0064, 0x003c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 } }, // ° (도)
  { '%', 16, { 0x0000, 0x0c3c, 0x066c, 0x0666, 0x0366, 0x036e, 0x01bc, 0x1ec0, 0x33c0, 0x3360, 0x3360, 0x3330, 0x1e18, 0x0000, 0x0000, 0x0000 } },
};

// ---------- 한글 상태 메시지용 글자 (웹페이지 글자 그리기로 만든 것) ----------
struct KGlyph { const char *ch; uint8_t w; uint16_t rows[16]; };
const KGlyph KGLYPHS[] = {
  { "와", 16, { 0x1800, 0x18f8, 0x1b8c, 0x1b0c, 0x1b06, 0x1b0c, 0xfb8c, 0x18f8, 0x1860, 0x1860, 0x1860, 0x1fff, 0x1800, 0x1800, 0x1800, 0x0000 } },
  { "이", 16, { 0x3000, 0x30f8, 0x319c, 0x330c, 0x330c, 0x3306, 0x3306, 0x330c, 0x338c, 0x319c, 0x30f8, 0x3000, 0x3000, 0x3000, 0x3000, 0x0000 } },
  { "파", 16, { 0x1800, 0x1bfe, 0x1800, 0x1998, 0x1998, 0x1998, 0xf998, 0x1998, 0x1998, 0x1998, 0x1fff, 0x1800, 0x1800, 0x1800, 0x1800, 0x0000 } },
  { "연", 16, { 0x30f0, 0x319c, 0x3f0c, 0x330c, 0x330c, 0x330c, 0x3f9c, 0x31f0, 0x3000, 0x3030, 0x3030, 0x0030, 0x0030, 0x3ff0, 0x0000, 0x0000 } },
  { "결", 16, { 0x3000, 0x31fc, 0x3f80, 0x3180, 0x30c0, 0x3e60, 0x3038, 0x000e, 0x3ff0, 0x3000, 0x3ff0, 0x0030, 0x0030, 0x3ff0, 0x0000, 0x0000 } },
  { "중", 16, { 0x3ffc, 0x0180, 0x03c0, 0x0e70, 0x781e, 0x0000, 0x7ffe, 0x0180, 0x0180, 0x0ff0, 0x1818, 0x1818, 0x1c38, 0x0ff0, 0x0000, 0x0000 } },
  { "안", 16, { 0x18f8, 0x198c, 0x1b06, 0x1b06, 0x1b06, 0xf98c, 0x18f8, 0x1800, 0x1800, 0x1818, 0x1818, 0x0018, 0x0018, 0x3ff8, 0x0000, 0x0000 } },
  { "됨", 16, { 0x3000, 0x33fc, 0x300c, 0x300c, 0x33fc, 0x3060, 0x3060, 0x3ffe, 0x0000, 0x3ff8, 0x3018, 0x3018, 0x3018, 0x3ff8, 0x0000, 0x0000 } },
  { "시", 16, { 0x3000, 0x3060, 0x3060, 0x3060, 0x3060, 0x3070, 0x3070, 0x30d8, 0x3198, 0x338c, 0x3706, 0x3000, 0x3000, 0x3000, 0x3000, 0x0000 } },
  { "간", 16, { 0x1800, 0x19fc, 0x1980, 0x1980, 0x18c0, 0xf860, 0x1830, 0x181c, 0x180e, 0x1800, 0x1818, 0x0018, 0x0018, 0x3ff8, 0x0000, 0x0000 } },
  { "못", 16, { 0x1ff8, 0x1818, 0x1818, 0x1818, 0x1ff8, 0x0180, 0x0180, 0x7ffe, 0x0000, 0x0180, 0x0180, 0x03c0, 0x0660, 0x0c30, 0x381c, 0x0000 } },
  { "받", 16, { 0x1800, 0x1986, 0x1986, 0x19fe, 0xf986, 0x1986, 0x19fe, 0x1800, 0x0000, 0x1ff8, 0x0018, 0x0018, 0x0018, 0x3ff8, 0x0000, 0x0000 } },
  { "음", 16, { 0x07e0, 0x1c38, 0x1818, 0x1818, 0x1c38, 0x07e0, 0x0000, 0x7ffe, 0x0000, 0x1ff8, 0x1818, 0x1818, 0x1818, 0x1ff8, 0x0000, 0x0000 } },
  { "날", 16, { 0x1800, 0x180c, 0x180c, 0xf80c, 0x180c, 0x180c, 0x1ffc, 0x0000, 0x1ff8, 0x1800, 0x1ff8, 0x0018, 0x0018, 0x3ff8, 0x0000, 0x0000 } },
  { "씨", 16, { 0x3000, 0x3198, 0x3198, 0x3198, 0x3198, 0x3198, 0x3398, 0x33fc, 0x37fc, 0x36e6, 0x3c66, 0x3030, 0x3000, 0x3000, 0x3000, 0x0000 } },
  { "패", 16, { 0x3600, 0x37fe, 0x3600, 0x36cc, 0x36cc, 0x36cc, 0x3ecc, 0x36cc, 0x36cc, 0x364c, 0x37fe, 0x3600, 0x3600, 0x3600, 0x3600, 0x0000 } },
  { "널", 16, { 0x3000, 0x300c, 0x300c, 0x3f8c, 0x300c, 0x300c, 0x37fc, 0x0000, 0x3ff0, 0x3000, 0x3ff0, 0x0030, 0x0030, 0x3ff0, 0x0000, 0x0000 } },
  { "성", 16, { 0x3000, 0x3060, 0x3060, 0x3f70, 0x30f0, 0x31d8, 0x330c, 0x3006, 0x0fe0, 0x3870, 0x3030, 0x3030, 0x3870, 0x0fc0, 0x0000, 0x0000 } },
  { "공", 16, { 0x1ff8, 0x1800, 0x1800, 0x1880, 0x1880, 0x0080, 0x7ffe, 0x0000, 0x0000, 0x0ff0, 0x1c18, 0x1818, 0x1c18, 0x0ff0, 0x0000, 0x0000 } },
};

// ---------- 날씨 그림 16×16 (Y 노랑, W 흰색, G 회색, B 파랑, . 꺼짐) ----------
const char *ICON_SUN[16] = {
  "................", ".......Y........", "..Y....Y....Y...", "...Y.......Y....",
  "......YYY.......", ".....YYYYY......", "....YYYYYYY.....", "YY..YYYYYYY..YY.",
  "....YYYYYYY.....", ".....YYYYY......", "......YYY.......", "...Y.......Y....",
  "..Y....Y....Y...", ".......Y........", "................", "................" };
const char *ICON_PARTLY[16] = {
  "................", "...Y............", "Y..Y..Y.........", ".YYYYY..........",
  ".YYYYY..WWW.....", "YYYYYYWWWWWW....", ".YYYWWWWWWWWW...", ".YYWWWWWWWWWWW..",
  "...WWWWWWWWWWWW.", "..WWWWWWWWWWWWW.", "..WWWWWWWWWWWWW.", "...WWWWWWWWWWW..",
  "................", "................", "................", "................" };
const char *ICON_CLOUD[16] = {
  "................", "................", "................", "......WWW.......",
  ".....WWWWW......", "....WWWWWWWW....", "..WWWWWWWWWWW...", ".WWWWWWWWWWWWW..",
  "WWWWWWWWWWWWWWW.", "WWWWWWWWWWWWWWW.", "WWWWWWWWWWWWWWW.", ".WWWWWWWWWWWWW..",
  "................", "................", "................", "................" };
const char *ICON_FOG[16] = {
  "................", "................", "................", "..GGGGGGGGGGG...",
  "................", "GGGGGGGGGGGGGG..", "................", "...GGGGGGGGGGGG.",
  "................", "GGGGGGGGGGGGG...", "................", "..GGGGGGGGGGGGG.",
  "................", "................", "................", "................" };
const char *ICON_RAIN[16] = {
  "......WWW.......", ".....WWWWW......", "...WWWWWWWWW....", "..WWWWWWWWWWW...",
  ".WWWWWWWWWWWWW..", ".WWWWWWWWWWWWW..", "..WWWWWWWWWWW...", "................",
  "..B...B...B.....", ".B...B...B......", "................", "....B...B...B...",
  "...B...B...B....", "................", "..B...B...B.....", ".B...B...B......" };
const char *ICON_SNOW[16] = {
  "......WWW.......", ".....WWWWW......", "...WWWWWWWWW....", "..WWWWWWWWWWW...",
  ".WWWWWWWWWWWWW..", ".WWWWWWWWWWWWW..", "..WWWWWWWWWWW...", "................",
  "..W....W....W...", ".WWW..WWW..WWW..", "..W....W....W...", "................",
  "....W....W......", "...WWW..WWW.....", "....W....W......", "................" };
const char *ICON_THUNDER[16] = {
  "......GGG.......", ".....GGGGG......", "...GGGGGGGGG....", "..GGGGGGGGGGG...",
  ".GGGGGGGGGGGGG..", ".GGGGGGGGGGGGG..", "..GGGGGYYGGGG...", "......YY........",
  ".....YY.........", "....YYYYY.......", ".......YY.......", "......YY........",
  ".....YY.........", ".....Y..........", "................", "................" };

// ---------- 날씨 화면 = 패널 글자 명령 (웹페이지에서 확인된 방식) ----------
// 글자 블록마다 색을 따로 줄 수 있어서: [날씨 그림(한 색)] [기온(주황)] [습도(파랑)]
// (처음엔 PNG 이미지로 보냈는데, 압축 안 한 PNG는 패널이 받기만 하고 화면이 꺼졌음)
struct RGB { uint8_t r, g, b; };
const RGB C_TEMP = { 255, 154, 31 }, C_HUMI = { 47, 184, 255 };

const Glyph *findGlyph(char c) {
  for (auto &g : GLYPHS) if (g.c == c) return &g;
  return nullptr;
}
// WMO 날씨 코드 → 그림과 색
const char *const *iconFor(int code, RGB &c) {
  c = { 220, 220, 230 }; // 흰색
  if (code == 0) { c = { 255, 200, 0 }; return ICON_SUN; }
  if (code <= 2) { c = { 255, 220, 120 }; return ICON_PARTLY; }
  if (code == 3) return ICON_CLOUD;
  if (code == 45 || code == 48) { c = { 150, 150, 170 }; return ICON_FOG; }
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICON_SNOW;
  if (code >= 95) { c = { 255, 200, 0 }; return ICON_THUNDER; }
  if (code >= 51) { c = { 40, 140, 255 }; return ICON_RAIN; }
  return ICON_CLOUD;
}

// 글자 명령 데이터 만들기 (웹페이지 buildTextPayload와 같은 형식)
// [글자 수] + 설정 13바이트 + 글자마다 [폭(00=8칸, 01=16칸), R, G, B, 16줄 점 그림(8칸당 1바이트, 왼쪽 칸 = 낮은 비트)]
std::vector<uint8_t> textPayload;
int textCount = 0;
void textBegin(uint8_t anim = 0) { // anim: 0 가만히, 1 왼쪽으로 흐르기
  textPayload = { 0, 0x00, 0x01, 0x01, anim, 80 /*빠르기*/, 0 /*무지개 끔*/, 255, 255, 255, 0, 0, 0, 0 };
  textCount = 0;
}
void textAddRows(const uint16_t *rows, uint8_t w, RGB c) {
  textPayload.push_back(w == 16 ? 0x01 : 0x00);
  textPayload.push_back(c.r); textPayload.push_back(c.g); textPayload.push_back(c.b);
  for (int y = 0; y < 16; y++) {
    textPayload.push_back(rows[y] & 0xFF);
    if (w == 16) textPayload.push_back(rows[y] >> 8);
  }
  textCount++;
}
void textAddGap() {
  static const uint16_t blank[16] = {};
  textAddRows(blank, 8, { 0, 0, 0 });
}
// 영문·숫자(GLYPHS)와 한글(KGLYPHS)을 섞어 쓸 수 있음. 공백은 8칸 빈칸
void textAddString(const char *s, RGB c) {
  while (*s) {
    if (*s == ' ') { textAddGap(); s++; continue; }
    if ((uint8_t)*s < 0x80) {
      if (auto g = findGlyph(*s)) textAddRows(g->rows, g->w, c);
      s++;
      continue;
    }
    bool found = false;
    for (auto &k : KGLYPHS) {
      size_t n = strlen(k.ch);
      if (strncmp(s, k.ch, n) == 0) { textAddRows(k.rows, k.w, c); s += n; found = true; break; }
    }
    if (!found) s++; // 모르는 글자는 건너뜀
  }
}
void textAddIcon(const char *const *icon, RGB c) {
  uint16_t rows[16];
  for (int y = 0; y < 16; y++) {
    rows[y] = 0;
    for (int x = 0; x < 16 && icon[y][x]; x++) if (icon[y][x] != '.') rows[y] |= 1 << x;
  }
  textAddRows(rows, 16, c);
}

// ---------- CRC32 (전송 확인용) ----------
uint32_t crcTable[256];
void initCrc() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) c = crcTable[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFF;
}


// ---------- 블루투스 (패널) ----------
NimBLEClient *client = nullptr;
NimBLERemoteCharacteristic *writeChar = nullptr;
NimBLEAddress panelAddr;
bool havePanelAddr = false;
volatile bool gotWin = false, gotAll = false;

void onNotify(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  Serial.print("  패널 응답:");
  for (size_t i = 0; i < n; i++) Serial.printf(" %02x", d[i]);
  Serial.println();
  if (n >= 5 && d[0] == 0x05) {
    if (d[4] == 0 || d[4] == 1) gotWin = true;
    else if (d[4] == 3) gotWin = gotAll = true;
  }
}

bool findPanel() {
  Serial.println("패널 찾는 중…");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults res = scan->getResults(6000, false);
  for (int i = 0; i < res.getCount(); i++) {
    const NimBLEAdvertisedDevice *d = res.getDevice(i);
    if (d->getName().rfind(PANEL_PREFIX, 0) == 0) {
      panelAddr = d->getAddress();
      havePanelAddr = true;
      Serial.printf("패널 찾음: %s (%s)\n", d->getName().c_str(), panelAddr.toString().c_str());
      return true;
    }
  }
  Serial.println("패널을 못 찾았어요 (폰이 연결 중이면 못 찾아요)");
  return false;
}

bool connectPanel() {
  if (!havePanelAddr && !findPanel()) return false;
  if (!client) client = NimBLEDevice::createClient();
  if (!client->connect(panelAddr)) {
    Serial.println("연결 실패 → 다음에 다시 찾기");
    havePanelAddr = false;
    return false;
  }
  NimBLERemoteService *svc = client->getService(NimBLEUUID((uint16_t)0xFA00));
  if (!svc) svc = client->getService(NimBLEUUID((uint16_t)0x00FA));
  if (!svc) { Serial.println("패널 서비스(fa00)를 못 찾았어요"); client->disconnect(); return false; }
  writeChar = nullptr;
  NimBLERemoteCharacteristic *notifyChar = nullptr;
  for (auto *c : svc->getCharacteristics(true)) {
    bool w = c->canWrite() || c->canWriteNoResponse();
    if (w && (!writeChar || c->getUUID() == NimBLEUUID((uint16_t)0xFA02))) writeChar = c;
    if (c->canNotify() && !notifyChar) notifyChar = c;
  }
  if (!writeChar) { Serial.println("쓰기 채널을 못 찾았어요"); client->disconnect(); return false; }
  if (notifyChar && notifyChar != writeChar) notifyChar->subscribe(true, onNotify);
  Serial.printf("패널 연결됨 (MTU %d)\n", client->getMTU());
  return true;
}
void disconnectPanel() {
  if (client && client->isConnected()) client->disconnect();
  writeChar = nullptr;
}

bool writeCmd(const uint8_t *data, size_t n) {
  return writeChar && writeChar->writeValue(data, n, writeChar->canWrite());
}
bool waitFlag(volatile bool &flag, uint32_t ms) {
  uint32_t t0 = millis();
  while (!flag && millis() - t0 < ms) delay(20);
  return flag;
}

// 큰 데이터 전송 (웹페이지 sendFramed와 같음). head: 이미지 = 02 00, 글자 = 00 01
bool sendFramed(uint8_t h0, uint8_t h1, const std::vector<uint8_t> &data) {
  uint8_t sz[4], cr[4];
  uint32_t size = data.size(), crc = crc32(data.data(), data.size());
  for (int i = 0; i < 4; i++) { sz[i] = size >> (8 * i); cr[i] = crc >> (8 * i); }
  size_t piece = min<size_t>(244, client->getMTU() - 3);
  gotAll = false;
  for (size_t pos = 0, idx = 0; pos < data.size(); pos += 12288, idx++) {
    size_t partLen = min<size_t>(12288, data.size() - pos);
    std::vector<uint8_t> msg;
    size_t len = 2 + 3 + 4 + 4 + 2 + partLen;
    msg.push_back(len & 0xFF); msg.push_back(len >> 8);
    msg.push_back(h0); msg.push_back(h1); msg.push_back(idx == 0 ? 0x00 : 0x02);
    msg.insert(msg.end(), sz, sz + 4);
    msg.insert(msg.end(), cr, cr + 4);
    msg.push_back(0x00); msg.push_back(0x00); // 저장 안 함, 바로 표시
    msg.insert(msg.end(), data.begin() + pos, data.begin() + pos + partLen);
    gotWin = false;
    for (size_t off = 0; off < msg.size(); off += piece) {
      if (!writeCmd(msg.data() + off, min(piece, msg.size() - off))) { Serial.println("쓰기 실패"); return false; }
    }
    if (!waitFlag(gotWin, 8000)) { Serial.println("패널이 받았다는 응답이 없어요"); return false; }
  }
  if (!waitFlag(gotAll, 8000)) Serial.println("완료 응답(03)은 안 왔어요");
  return true;
}

// ---------- 시간·날씨 ----------
bool timeOk() { return time(nullptr) > 1700000000; }

float outTemp = NAN; int outHumi = -1, outCode = -1;
uint32_t lastWeather = 0;
bool haveWeather = false;

// 와이파이가 끊긴/실패한 이유를 쉬운 말로 기록
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("와이파이 연결됨 (IP %s)\n", WiFi.localIP().toString().c_str());
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    int r = info.wifi_sta_disconnected.reason;
    // 참고: 이유 2·4·201·36은 신호가 약할 때도 자주 나옴 (실제로 비밀번호가 맞아도 약한 신호에서 반복됨)
    const char *why = r == 201 ? "와이파이를 못 찾음 (신호 약함 또는 2.4GHz 아님)"
                    : (r == 15 || r == 204) ? "비밀번호 확인 단계 실패 (비밀번호 또는 약한 신호)"
                    : (r == 2 || r == 4 || r == 36 || r == 200) ? "연결 도중 끊김 (신호 약함)"
                    : "기타";
    Serial.printf("와이파이 끊김/실패: 이유 %d → %s\n", r, why);
  }
}

// 처음 한 번 주변 와이파이 목록을 보여줌 (문제 찾기용)
void listNetworks() {
  Serial.println("주변 와이파이:");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 15; i++) {
    Serial.printf("  %-24s 신호 %d  채널 %d%s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  WiFi.SSID(i) == WIFI_SSID ? "  ← 우리 와이파이" : "");
  }
  WiFi.scanDelete();
}

bool wifiStarted = false;
void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (!wifiStarted) { // begin은 한 번만. 이후에는 ESP32가 알아서 다시 연결 시도함
    Serial.printf("와이파이 연결 중: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiStarted = true;
  }
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  if (WiFi.status() != WL_CONNECTED) Serial.println("와이파이 아직 연결 안 됨");
}

// 인터넷 주소 찾기(DNS)가 되는지 확인. 안 되면 구글·클라우드플레어 DNS로 바꿈
bool dnsFixed = false;
bool checkDns(const char *host) {
  IPAddress ip;
  if (WiFi.hostByName(host, ip) == 1) return true;
  Serial.printf("주소 찾기 실패: %s (DNS %s)\n", host, WiFi.dnsIP().toString().c_str());
  if (!dnsFixed) {
    dnsFixed = true;
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
    delay(500);
    Serial.println("→ DNS를 8.8.8.8로 바꿨어요");
    if (WiFi.hostByName(host, ip) == 1) return true;
    Serial.println("→ 그래도 주소를 못 찾았어요");
  }
  return false;
}

void updateWeather() {
  connectWifi();
  if (WiFi.status() != WL_CONNECTED) return;
  lastWeather = millis() - (WEATHER_UPDATE_MINUTES - 1) * 60000UL; // 실패하면 1분 뒤 다시 (성공하면 아래에서 덮어씀)
  if (!checkDns("api.open-meteo.com")) return;
  WiFiClient net; // 날씨는 비밀 정보가 아니라서 일반 http로 받음 (보안 접속보다 가볍고 확실함)
  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia%%2FSeoul", LAT, LON);
  Serial.println("날씨 받는 중…");
  if (!http.begin(net, url)) { Serial.println("날씨 주소 열기 실패"); return; }
  http.useHTTP10(true); // 응답을 조각(chunked) 없이 한 번에 받게 함 → JSON을 바로 읽을 수 있음
  http.setTimeout(10000);
  int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    if (!err) {
      outTemp = doc["current"]["temperature_2m"] | NAN;
      outHumi = doc["current"]["relative_humidity_2m"] | -1;
      outCode = doc["current"]["weather_code"] | -1;
      haveWeather = true;
      lastWeather = millis();
      Serial.printf("날씨: %.1f도, 습도 %d%%, 코드 %d\n", outTemp, outHumi, outCode);
    } else {
      Serial.printf("날씨 내용 읽기 실패: %s\n", err.c_str());
    }
  } else {
    Serial.printf("날씨 받기 실패 (HTTP %d %s)\n", code, http.errorToString(code).c_str());
  }
  http.end();
}

// ---------- 화면들 ----------
bool showClock() {
  if (!timeOk()) return false;
  struct tm t; getLocalTime(&t);
  uint8_t setTime[] = { 0x08, 0x00, 0x01, 0x80, (uint8_t)t.tm_hour, (uint8_t)t.tm_min, (uint8_t)t.tm_sec, 0x00 };
  uint8_t dow = t.tm_wday == 0 ? 7 : t.tm_wday; // 월=1 … 일=7
  uint8_t clock[] = { 0x0b, 0x00, 0x06, 0x01, CLOCK_STYLE, 0x01, (uint8_t)(CLOCK_SHOW_DATE ? 1 : 0),
                      (uint8_t)(t.tm_year % 100), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday, dow };
  bool ok = writeCmd(setTime, sizeof(setTime));
  delay(100);
  ok = ok && writeCmd(clock, sizeof(clock));
  delay(300); // 응답 기록용
  Serial.printf("시계 보냄 %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
  return ok;
}

bool showWeather() {
  if (!haveWeather) return false;
  char temp[8], humi[8];
  snprintf(temp, sizeof(temp), "%dd", (int)lroundf(outTemp)); // d = ° 모양
  snprintf(humi, sizeof(humi), "%d%%", outHumi);
  RGB iconColor;
  const char *const *icon = iconFor(outCode, iconColor);
  textBegin();
  textAddIcon(icon, iconColor); // 16칸
  textAddGap();                 // 8칸
  textAddString(temp, C_TEMP);  // 예: 20° = 24칸
  if (strlen(temp) < 4) textAddGap(); // -12°처럼 길면 틈 생략 (가로 96칸 안에 맞춤)
  textAddString(humi, C_HUMI);  // 예: 76% = 32칸
  textPayload[0] = textCount;
  Serial.printf("날씨 화면 보냄 (글자 %d개, %u바이트)\n", textCount, (unsigned)textPayload.size());
  return sendFramed(0x00, 0x01, textPayload);
}

// 연결 → 보내기 → 끊기
// 블루투스는 패널에 보낼 때만 켜고 끝나면 완전히 끔.
// ESP32는 안테나 하나를 와이파이와 나눠 쓰는데, 블루투스가 켜져 있으면 약한 와이파이에서 인터넷이 자주 실패했음.
void bleOn() {
  NimBLEDevice::init("");
  NimBLEDevice::setMTU(517);
}
void bleOff() {
  NimBLEDevice::deinit(true); // 만든 연결(client)도 함께 지워짐
  client = nullptr;
  writeChar = nullptr;
}
bool withPanel(bool (*show)()) {
  bleOn();
  bool ok = connectPanel() && show();
  disconnectPanel();
  bleOff();
  return ok;
}

// ---------- 상태 메시지 (PC 없이도 무엇이 문제인지 패널에서 보이게) ----------
const char *statusMsg = nullptr;   // 보낼 메시지
const char *lastStatus = nullptr;  // 마지막으로 보낸 메시지 (같은 걸 반복해서 보내지 않음)
const RGB C_STATUS = { 255, 180, 60 };
bool showStatusNow() {
  textBegin(1); // 왼쪽으로 흐르기 (96칸보다 길어서)
  textAddString(statusMsg, C_STATUS);
  textPayload[0] = textCount;
  Serial.printf("패널에 상태 표시: %s\n", statusMsg);
  return sendFramed(0x00, 0x01, textPayload);
}
void showStatus(const char *msg) {
  if (lastStatus && strcmp(lastStatus, msg) == 0) return;
  statusMsg = msg;
  if (withPanel(showStatusNow)) lastStatus = msg;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== LED 패널 시계·날씨 ===");
  initCrc();
  showStatus("패널 연결 성공"); // 블루투스는 보낼 때만 잠깐 켬 (withPanel)
  delay(4000);

  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  listNetworks();
  showStatus("와이파이 연결 중");
  connectWifi();
  syncTime();
  updateWeather();
}

void syncTime() {
  if (WiFi.status() != WL_CONNECTED) return;
  checkDns("pool.ntp.org");
  configTzTime("KST-9", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  for (int i = 0; i < 40 && !timeOk(); i++) delay(250);
  Serial.println(timeOk() ? "인터넷 시간 맞춤" : "인터넷 시간을 아직 못 받았어요 (30초 뒤 다시)");
}

void loop() {
  // 1) 와이파이
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi(); // 최대 15초 기다림
    if (WiFi.status() != WL_CONNECTED) { showStatus("와이파이 안 됨"); delay(5000); return; }
  }
  // 2) 인터넷 시간
  static uint32_t lastTimeTry = 0;
  if (!timeOk() && (lastTimeTry == 0 || millis() - lastTimeTry > 30000)) { lastTimeTry = millis(); syncTime(); }
  if (!timeOk()) { showStatus("시간 못 받음"); delay(5000); return; }
  // 3) 날씨 (실패해도 시계는 계속 보여줌)
  if (lastWeather == 0 || millis() - lastWeather > WEATHER_UPDATE_MINUTES * 60000UL) updateWeather();
  if (!haveWeather) showStatus("날씨 못 받음");

  // 4) 시계 → 날씨 번갈아
  if (withPanel(showClock)) { lastStatus = nullptr; delay(CLOCK_SECONDS * 1000); } // 정상이면 다음 문제는 다시 표시되게
  else delay(5000);
  if (haveWeather && withPanel(showWeather)) delay(WEATHER_SECONDS * 1000);
}

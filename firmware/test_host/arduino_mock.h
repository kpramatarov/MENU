// ============================================================================
//  Имитация на Arduino/ESP8266 API за тестове на персонален компютър.
//  Скицата (.ino) се компилира без промени с g++; времето е симулирано
//  (g_us, µs), а хардуерните функции "струват" типичното си време.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cctype>
#include <math.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <functional>

// ---------------------------------------------------------------- време -----
inline uint64_t g_us = 1000000ULL;                 // начало: 1 s след стартиране
inline unsigned long millis() { return (unsigned long)(g_us / 1000ULL); }
inline uint32_t micros() { return (uint32_t)g_us; } // 32 бита, както на ESP8266
inline void delay(unsigned long ms) { g_us += (uint64_t)ms * 1000ULL; }
inline void delayMicroseconds(unsigned int us) { g_us += us; }

// --------------------------------------------------------------- макроси ----
#define IRAM_ATTR
#define ICACHE_RAM_ATTR
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
typedef const char *PGM_P;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 3

static const uint8_t D0 = 16, D1 = 5, D2 = 4, D3 = 0, D4 = 2, D5 = 14, D6 = 12, D7 = 13, D8 = 15;
static const uint8_t A0 = 17;

// ---------------------------------------------------------------- String ----
class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  const char *c_str() const { return s.c_str(); }
  unsigned int length() const { return (unsigned int)s.size(); }
  void trim() {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) b++;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
    s = s.substr(b, e - b);
  }
  void toUpperCase() { for (auto &ch : s) ch = (char)toupper((unsigned char)ch); }
  bool startsWith(const String &p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  int indexOf(char c, unsigned int from = 0) const {
    size_t pos = s.find(c, from);
    return pos == std::string::npos ? -1 : (int)pos;
  }
  String substring(unsigned int b) const { return b > s.size() ? String() : String(s.substr(b)); }
  String substring(unsigned int b, unsigned int e) const {
    if (b > e) { unsigned int t = b; b = e; e = t; }
    if (b > s.size()) return String();
    if (e > s.size()) e = (unsigned int)s.size();
    return String(s.substr(b, e - b));
  }
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return (float)atof(s.c_str()); }
  bool operator==(const char *o) const { return s == o; }
  bool operator==(const String &o) const { return s == o.s; }
};

// ------------------------------------------------------------- IPAddress ----
struct IPAddress {
  uint8_t b[4];
  IPAddress(uint8_t a = 0, uint8_t c = 0, uint8_t d = 0, uint8_t e = 0) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  uint8_t operator[](int i) const { return b[i]; }
  String toString() const {
    char t[16]; snprintf(t, sizeof(t), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); return String(t);
  }
};

// ---------------------------------------------------------------- Serial ----
struct MockSerial {
  std::string out;                 // целият изход (за проверки)
  std::deque<std::string> in;      // въведени редове
  void begin(unsigned long) {}
  void setTimeout(unsigned long) {}
  int available() { return in.empty() ? 0 : (int)in.front().size() + 1; }
  String readStringUntil(char) { std::string l = in.front(); in.pop_front(); return String(l); }
  size_t print(const char *s) { out += s; return strlen(s); }
  size_t print(const String &s) { return print(s.c_str()); }
  size_t print(char c) { out += c; return 1; }
  size_t print(unsigned char v) { return print((unsigned long)v); }
  size_t print(int v) { return print((long)v); }
  size_t print(unsigned int v) { return print((unsigned long)v); }
  size_t print(long v) { char t[24]; snprintf(t, sizeof(t), "%ld", v); return print(t); }
  size_t print(unsigned long v) { char t[24]; snprintf(t, sizeof(t), "%lu", v); return print(t); }
  size_t print(double v, int digits = 2) { char t[48]; snprintf(t, sizeof(t), "%.*f", digits, v); return print(t); }
  size_t print(const IPAddress &ip) { return print(ip.toString()); }
  template <typename T> size_t println(const T &v) { size_t n = print(v); out += "\n"; return n + 1; }
  size_t println(double v, int digits) { size_t n = print(v, digits); out += "\n"; return n + 1; }
  size_t println() { out += "\n"; return 1; }
  size_t printf_P(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char t[512]; va_list ap; va_start(ap, fmt); int n = vsnprintf(t, sizeof(t), fmt, ap); va_end(ap);
    out += t; return (size_t)n;
  }
};
inline MockSerial Serial;

// ------------------------------------------------------------------- GPIO ---
inline int g_pin[32];                              // ниво на всеки извод
inline int g_pinMode[32];
inline void (*g_isr[32])() = {};
inline int g_analog = 470;                         // стойност на A0 (0...1023)
inline int g_analogReads = 0;
inline int digitalRead(uint8_t p) { g_us += 1; return g_pin[p]; }
inline void digitalWrite(uint8_t p, uint8_t v) { g_us += 1; g_pin[p] = v ? 1 : 0; }
inline void pinMode(uint8_t p, uint8_t m) { g_pinMode[p] = m; if (m == INPUT_PULLUP) g_pin[p] = 1; }
inline int analogRead(uint8_t) { g_us += 90; g_analogReads++; return g_analog; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int p, void (*f)(), int) { g_isr[p] = f; }
inline void noInterrupts() {}
inline void interrupts() {}
// Промяна на нивото на входен извод от теста (с извикване на прекъсването)
inline void setPin(uint8_t p, int level) {
  if (g_pin[p] != level) { g_pin[p] = level; if (g_isr[p]) g_isr[p](); }
}

// -------------------------------------------------------------------- ESP ---
struct RestartException {};
struct MockESP {
  uint32_t freeHeap = 45000, maxBlock = 30000, rnd = 0x7FFFFFF0u; uint8_t frag = 4;
  uint32_t getFreeHeap() { return freeHeap; }
  void getHeapStats(uint32_t *f, uint32_t *m, uint8_t *fr) {
    g_us += 150; if (f) *f = freeHeap; if (m) *m = maxBlock; if (fr) *fr = frag;
  }
  String getResetReason() { return String("External System"); }
  void restart() { throw RestartException(); }
  uint32_t random() { return rnd; }
};
inline MockESP ESP;

// ----------------------------------------------------------------- EEPROM ---
struct MockEEPROM {
  uint8_t data[4096]; int commits = 0;
  MockEEPROM() { memset(data, 0xFF, sizeof(data)); }
  void begin(size_t) {}
  template <typename T> T &get(int a, T &t) { memcpy(&t, data + a, sizeof(T)); return t; }
  template <typename T> const T &put(int a, const T &t) { memcpy(data + a, &t, sizeof(T)); return t; }
  bool commit() { commits++; g_us += 30000; return true; }
};
inline MockEEPROM EEPROM;

// ------------------------------------------------------------------- WiFi ---
#define WIFI_STA 1
#define WIFI_AP_STA 3
#define WIFI_NONE_SLEEP 0
#define WL_CONNECTED 3
struct MockWiFi {
  int ch = 6; int32_t rssi = -55; std::string mac = "BC:FF:4D:1D:A5:A6";
  void mode(int) {}
  void setSleepMode(int) {}
  void hostname(const char *) {}
  void begin(const char *, const char *) {}
  int status() { return WL_CONNECTED; }
  IPAddress localIP() { return IPAddress(192, 168, 1, 13); }
  int32_t channel() { return ch; }
  int32_t RSSI() { return rssi; }
  String macAddress() { return String(mac); }
  bool softAP(const char *, const char *, int, int) { return true; }
};
inline MockWiFi WiFi;

// ---------------------------------------------------------------- ESP-NOW ---
#define ESP_NOW_ROLE_COMBO 3
typedef void (*esp_now_recv_cb_t)(uint8_t *, uint8_t *, uint8_t);
typedef void (*esp_now_send_cb_t)(uint8_t *, uint8_t);
struct SentPacket { uint8_t mac[6]; std::vector<uint8_t> data; uint64_t t_us; };
inline std::vector<SentPacket> g_sent;
inline esp_now_recv_cb_t g_recv_cb = nullptr;
inline esp_now_send_cb_t g_send_cb = nullptr;
inline int esp_now_init() { return 0; }
inline int esp_now_set_self_role(uint8_t) { return 0; }
inline int esp_now_register_recv_cb(esp_now_recv_cb_t cb) { g_recv_cb = cb; return 0; }
inline int esp_now_register_send_cb(esp_now_send_cb_t cb) { g_send_cb = cb; return 0; }
inline int esp_now_add_peer(uint8_t *, uint8_t, uint8_t, uint8_t *, uint8_t) { return 0; }
inline int esp_now_set_kok(uint8_t *, uint8_t) { return 0; }
inline int esp_now_send(uint8_t *mac, uint8_t *data, int len) {
  g_us += 60;
  SentPacket p; memcpy(p.mac, mac, 6); p.data.assign(data, data + len); p.t_us = g_us;
  g_sent.push_back(p);
  return 0;
}

// -------------------------------------------------------------------- DHT ---
#define DHT11 11
struct DHT {
  static inline float t = 22.9f, h = 34.0f;
  static inline bool fail = false;
  static inline int reads = 0;
  DHT(uint8_t, uint8_t) {}
  void begin() {}
  bool read(bool = false) { reads++; delay(21); g_us += 4500; return !fail; }   // 1 + 20 ms + ~4.5 ms
  float readHumidity(bool = false) { return fail ? NAN : h; }
  float readTemperature(bool = false, bool = false) { return fail ? NAN : t; }
};

// ------------------------------------------------------------- Уеб сървър ---
struct MockServer {
  std::map<std::string, std::function<void()>> routes;
  std::map<std::string, std::string> args;
  int code = 0; std::string type, body;
  explicit MockServer(int) {}
  void on(const char *p, std::function<void()> f) { routes[p] = f; }
  void begin() {}
  void handleClient() {}
  bool hasArg(const char *n) { return args.count(n) > 0; }
  String arg(const char *n) { auto it = args.find(n); return it == args.end() ? String() : String(it->second); }
  bool authenticate(const char *, const char *) { return true; }
  void requestAuthentication() {}
  void send(int c, const char *t, const char *b) { code = c; type = t; body = b; }
  void send(int c, const char *t, const char *b, size_t n) { code = c; type = t; body.assign(b, n); }
  void send_P(int c, const char *t, const char *b) { send(c, t, b); }
  // Извикване на обработчик от теста
  void call(const char *path, std::map<std::string, std::string> a = {}) { args = a; routes.at(path)(); }
};
typedef MockServer ESP8266WebServer;

struct MockOTA { void setHostname(const char *) {} void setPassword(const char *) {} void begin() {} void handle() {} };
inline MockOTA ArduinoOTA;
struct MockMDNS { bool begin(const char *) { return true; } void addService(const char *, const char *, int) {} void update() {} };
inline MockMDNS MDNS;
struct MockLLMNR { bool begin(const char *) { return true; } };
inline MockLLMNR LLMNR;

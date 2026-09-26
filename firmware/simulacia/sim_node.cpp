// ============================================================================
//  Обвивка за симулацията на системата: непроменената скица (Slave.ino или
//  Master.ino) заедно с имитацията на хардуера (../test_host/arduino_mock.h)
//  се компилира като споделена библиотека. Всяко зареждане на библиотеката е
//  "включване на захранването" - глобалните променливи се инициализират наново,
//  а съдържанието на EEPROM се пази от симулатора.
//  Компилиране: виж run_sim.sh (-DNODE_SLAVE или -DNODE_MASTER).
// ============================================================================
#include "../test_host/arduino_mock.h"
#if defined(NODE_SLAVE)
#include "../v3/Slave/Slave.ino"
#elif defined(NODE_MASTER)
#include "../v3/Master/Master.ino"
#else
#error "NODE_SLAVE или NODE_MASTER"
#endif

#define API extern "C" __attribute__((visibility("default")))

static size_t g_taken = 0;       // брой изпратени пакети, предадени на симулатора

API int sim_setup() {
  try { setup(); return 0; } catch (RestartException &) { return 1; }
}
API int sim_loop() {
  try { loop(); return 0; } catch (RestartException &) { return 1; }
}
API uint64_t sim_now() { return g_us; }
API void sim_set_now(uint64_t t) { g_us = t; }
API void sim_set_pin(int p, int level) { setPin((uint8_t)p, level); }
API int sim_get_pin(int p) { return g_pin[p]; }
API void sim_set_analog(int v) { g_analog = v; }
API void sim_set_random(uint32_t r) { ESP.rnd = r; }
API void sim_dht(int fail) { DHT::fail = fail != 0; }
#if defined(NODE_SLAVE)
API int sim_locked() { return isSafetyLocked ? 1 : 0; }
#endif

// Следващият изпратен пакет: връща дължината или -1, ако няма
API int sim_take_sent(uint8_t *mac, uint8_t *buf, int max, uint64_t *t) {
  if (g_taken >= g_sent.size()) {
    if (!g_sent.empty()) { g_sent.clear(); g_taken = 0; }
    return -1;
  }
  const SentPacket &p = g_sent[g_taken++];
  memcpy(mac, p.mac, 6);
  int n = (int)p.data.size() < max ? (int)p.data.size() : max;
  memcpy(buf, p.data.data(), n);
  *t = p.t_us;
  return n;
}
API void sim_deliver(const uint8_t *mac, const uint8_t *d, int len) {
  if (g_recv_cb) g_recv_cb((uint8_t *)mac, (uint8_t *)d, (uint8_t)len);
}
API void sim_status(const uint8_t *mac, int st) {
  if (g_send_cb) g_send_cb((uint8_t *)mac, (uint8_t)st);
}
API void sim_serial_in(const char *line) { Serial.in.push_back(line); }
// Изведеният по UART текст от последното извикване насам
API size_t sim_serial_take(char *buf, size_t max) {
  size_t n = Serial.out.size() < max - 1 ? Serial.out.size() : max - 1;
  memcpy(buf, Serial.out.data(), n);
  buf[n] = 0;
  Serial.out.erase(0, n);
  return n;
}
API void sim_eeprom_get(uint8_t *b) { memcpy(b, EEPROM.data, sizeof(EEPROM.data)); }
API void sim_eeprom_set(const uint8_t *b) { memcpy(EEPROM.data, b, sizeof(EEPROM.data)); }

#if defined(NODE_MASTER)
// Заявка към уеб сървъра; връща кода и копира отговора
API int sim_http(const char *path, const char *key, const char *val, char *body, int max) {
  std::map<std::string, std::string> a;
  if (key && *key) a[key] = val;
  server.call(path, a);
  if (body && max > 0) {
    size_t n = server.body.size() < (size_t)max - 1 ? server.body.size() : (size_t)max - 1;
    memcpy(body, server.body.data(), n); body[n] = 0;
  }
  return server.code;
}
#endif

// Вътрешни величини за проверките на симулатора
API double sim_get(const char *name) {
  std::string n(name);
#if defined(NODE_SLAVE)
  if (n == "locked")    return isSafetyLocked;
  if (n == "trips")     return safetyTrips;
  if (n == "refusals")  return safetyRefusals;
  if (n == "presses0")  return btnPresses[0];
  if (n == "presses1")  return btnPresses[1];
  if (n == "edges0")    return btnEdges[0];
  if (n == "edges1")    return btnEdges[1];
  if (n == "maxedges0") return btnMaxEdges[0];
  if (n == "rejected")  return cmdRejected;
  if (n == "reading")   return readingCounter;
  if (n == "dhtErrors") return dhtErrors;
  if (n == "success")   return successPackets;
  if (n == "failed")    return failedPackets;
  if (n == "ackSeq")    return ackSeq;
  if (n == "interval")  return cfg.telemetryInterval;
  for (int i = 0; i < 4; i++)
    if (n == "relay" + std::to_string(i)) return digitalRead(relayPins[i]);
#else
  if (n == "rx")        return rxPackets;
  if (n == "lost")      return rxLost;
  if (n == "dup")       return rxDup;
  if (n == "bad")       return rxBad;
  if (n == "reboots")   return slaveReboots;
  if (n == "qCount")    return qCount;
  if (n == "qState")    return (int)qState;
  if (n == "pingRunning") return pingRunning;
  if (n == "pingSent")  return pingSent;
  if (n == "pingOk")    return pingOk;
  if (n == "pingLost")  return pingLost;
  if (n == "ackCount")  return ackCount;
  if (n == "ackFail")   return ackFail;
  if (n == "cmdSeq")    return cmdSeq;
  if (n == "everReceived") return everReceived;
  if (n == "lastDataTime") return lastDataTime;
  if (n == "errorTime") return lastErrorTime;
  if (n == "jsonOverflows") return jsonOverflowCount;
  if (n == "linkTimeout") return cfg.linkTimeout;
  for (int i = 0; i < 4; i++)
    if (n == "rs" + std::to_string(i)) return currentRelayState[i];
#endif
  return -1e300;
}

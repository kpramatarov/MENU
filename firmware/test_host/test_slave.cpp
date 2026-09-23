// ============================================================================
//  Тестове на логиката на подчинения модул (Slave.ino) на персонален компютър
// ============================================================================
#include "arduino_mock.h"
#include "../v3/Slave/Slave.ino"

static int passes = 0, fails = 0;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

static const uint8_t MASTER_MAC[6] = {0xA4, 0xE5, 0x7C, 0x01, 0xE6, 0x9E};
static const uint8_t OTHER_MAC[6]  = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static bool autoAck = true;
static size_t acked = 0;

// Итерации на loop() за зададено време; между итерациите "системата" работи 20 µs
static void runFor(uint64_t us) {
  uint64_t end = g_us + us;
  while (g_us < end) {
    loop();
    if (autoAck) while (acked < g_sent.size()) { g_send_cb(g_sent[acked].mac, 0); acked++; }
    else acked = g_sent.size();
    g_us += 20;
  }
}
static void runUntilMs(unsigned long ms) { if (millis() < ms) runFor((uint64_t)(ms - millis()) * 1000ULL); }

static struct_message packet(size_t i) {
  struct_message m; memcpy(&m, g_sent.at(i).data.data(), sizeof(m)); return m;
}
static struct_message lastPacket() { return packet(g_sent.size() - 1); }

static struct_control mkCmd(uint8_t type, uint8_t idx, bool st, uint32_t seq) {
  struct_control c; memset(&c, 0, sizeof(c));
  c.protoVersion = PROTO_VERSION; c.cmdType = type; c.relayIndex = idx; c.relayState = st;
  c.telemetryInterval = 2000; c.vccOffset = 0.0f; c.wifiChannel = 6; c.seq = seq;
  return c;
}
static void deliver(const struct_control &c, const uint8_t *mac = MASTER_MAC) {
  g_recv_cb((uint8_t *)mac, (uint8_t *)&c, sizeof(c));
}
static bool printed(const char *s) { return Serial.out.find(s) != std::string::npos; }

int main() {
  // --- 1. Миграция на конфигурацията от версия 2 ---------------------------
  SlaveConfig v2; memset(&v2, 0, sizeof(v2));
  v2.magic = CFG_MAGIC; v2.version = 2; memcpy(v2.peerMac, MASTER_MAC, 6);
  v2.wifiChannel = 6; v2.telemetryInterval = 2000; v2.vccOffset = 0.30f;
  v2.crc = crc32((uint8_t *)&v2, sizeof(v2) - sizeof(v2.crc));
  EEPROM.put(0, v2);

  setup();
  CHECK(cfg.version == 3);
  CHECK(cfg.vccOffset == 0.0f);
  CHECK(cfg.telemetryInterval == 2000 && cfg.wifiChannel == 6);
  CHECK(EEPROM.commits == 1);
  SlaveConfig stored; EEPROM.get(0, stored);
  CHECK(stored.version == 3 && stored.crc == crc32((uint8_t *)&stored, sizeof(stored) - sizeof(stored.crc)));
  CHECK(printed("версия 2 е прехвърлена"));

  // Изводи: реле 4 е на GPIO15 (D8); всички релета са изходи на ниско ниво
  CHECK(relayPins[0] == 16 && relayPins[1] == 5 && relayPins[2] == 4 && relayPins[3] == 15);
  for (int i = 0; i < 4; i++) CHECK(g_pinMode[relayPins[i]] == OUTPUT && g_pin[relayPins[i]] == LOW);
  CHECK(g_isr[D5] != nullptr && g_isr[D6] != nullptr);
  CHECK(lastA0Sum == 4700);                              // първото измерване е в setup()
  CHECK(sizeof(struct_message) <= 250);
  printf("sizeof(struct_message) = %zu B, sizeof(struct_control) = %zu B\n",
         sizeof(struct_message), sizeof(struct_control));

  // --- 2. Периодична телеметрия ------------------------------------------------
  g_sent.clear(); acked = 0;
  runUntilMs(2100);
  CHECK(g_sent.size() == 1);
  struct_message p = lastPacket();
  CHECK(p.protoVersion == 3 && p.fwVersion == 3 && p.reportType == REPORT_PERIODIC);
  CHECK(p.readingId == 1 && p.ackSeq == 0);
  CHECK(p.dhtValid && NEAR(p.temperature, 22.9, 1e-4) && NEAR(p.humidity, 34.0, 1e-4));
  CHECK(p.a0Sum == 4700);
  CHECK(NEAR(p.vcc, 470.0 / 1024.0 * 10.91, 1e-4));
  CHECK(p.hr202State && !p.safetyLocked);
  CHECK(!p.relayState[0] && !p.relayState[1] && !p.relayState[2] && !p.relayState[3]);
  CHECK(strcmp(p.resetReason, "External System") == 0);
  CHECK(p.cfgInterval == 2000 && p.cfgChannel == 6 && p.cfgVccOffset == 0.0f);
  // Изпращането е след четенето на DHT11 (~25.5 ms) и 10-те проби през 2 ms (~18 ms)
  double sendMs = g_sent[0].t_us / 1000.0;
  printf("periodic #1 sent at %.2f ms; loop avg %u us, max %u us, period max %u us, count %u, window %u ms\n",
         sendMs, p.loopAvgUs, p.loopMaxUs, p.periodMaxUs, p.loopCount, p.windowMs);
  CHECK(sendMs > 2040 && sendMs < 2060);
  CHECK(p.loopMaxUs >= 25500 && p.loopMaxUs < 30000);     // четенето на DHT11 е най-дългата итерация
  CHECK(p.loopAvgUs > 0 && p.loopAvgUs < 100);
  CHECK(p.periodMaxUs >= p.loopMaxUs);
  CHECK(p.loopMaxEverUs >= p.loopMaxUs);
  CHECK(p.loopCount > 10000);
  CHECK((uint64_t)p.loopCount * p.loopAvgUs <= (uint64_t)p.windowMs * 1000ULL);
  CHECK(p.windowMs > 900 && p.windowMs < 1100);
  CHECK(DHT::reads == 1);

  runUntilMs(4100);
  CHECK(g_sent.size() == 2);
  p = lastPacket();
  CHECK(p.readingId == 2 && p.windowMs >= 1995 && p.windowMs <= 2005);
  CHECK(p.successPackets == 1);                           // потвърждението на първия пакет
  CHECK(DHT::reads == 2);

  // --- 3. Команда за реле 4 (D8) и незабавен отчет ---------------------------
  size_t before = g_sent.size();
  uint64_t tCmd = g_us;
  deliver(mkCmd(CMD_RELAY, 3, true, 1000));
  runFor(1000);
  CHECK(g_pin[15] == HIGH && currentRelayState[3]);
  CHECK(g_sent.size() == before + 1);
  p = lastPacket();
  CHECK(p.reportType == REPORT_COMMAND && p.ackSeq == 1000 && p.relayState[3]);
  printf("command -> report: %.3f ms\n", (g_sent.back().t_us - tCmd) / 1000.0);
  CHECK(g_sent.back().t_us - tCmd < 1000);

  // Корекцията за включено реле се прилага при следващото измерване
  runUntilMs(6100);
  p = lastPacket();
  CHECK(p.reportType == REPORT_PERIODIC);
  CHECK(NEAR(p.vcc, 470.0 / 1024.0 * 10.91 - 0.055, 1e-4));

  // --- 4. Дубликат, чужд MAC, друга версия -----------------------------------
  uint16_t rej0 = cmdRejected;
  before = g_sent.size();
  deliver(mkCmd(CMD_RELAY, 3, false, 1000));            // същият номер
  runFor(1000);
  CHECK(cmdRejected == rej0 + 1 && g_pin[15] == HIGH && g_sent.size() == before);

  deliver(mkCmd(CMD_RELAY, 3, false, 1001), OTHER_MAC);  // чужд изпращач
  runFor(1000);
  CHECK(cmdRejected == rej0 + 2 && g_pin[15] == HIGH);
  CHECK(printed("непознат MAC"));

  struct_control old = mkCmd(CMD_RELAY, 3, false, 1002);
  old.protoVersion = 2;                                   // пакет от фърмуер v2
  deliver(old);
  runFor(1000);
  CHECK(cmdRejected == rej0 + 3 && g_pin[15] == HIGH);
  CHECK(!printed("протокол v2"));                         // ограничение 5 s между съобщенията
  runFor(5100000);
  CHECK(printed("протокол v2"));

  // --- 5. Защита от влага ---------------------------------------------------
  deliver(mkCmd(CMD_RELAY, 0, true, 1003));
  runFor(1000);
  CHECK(g_pin[16] == HIGH);
  before = g_sent.size();
  setPin(D7, LOW);                                        // влага
  runFor(1000);
  for (int i = 0; i < 4; i++) CHECK(g_pin[relayPins[i]] == LOW);   // изключване веднага
  CHECK(isSafetyLocked && safetyTrips == 1);
  CHECK(g_sent.size() == before);                         // отчетът чака 50 ms след предишния
  runFor(60000);
  CHECK(g_sent.size() == before + 1);
  p = lastPacket();
  CHECK(p.reportType == REPORT_EVENT && p.safetyLocked && !p.hr202State);

  deliver(mkCmd(CMD_RELAY, 1, true, 1004));               // включване по време на блокировка
  runFor(1000);
  CHECK(g_pin[5] == LOW && safetyRefusals == 1);
  p = lastPacket();
  CHECK(p.reportType == REPORT_COMMAND && p.ackSeq == 1004 && !p.relayState[1]);

  setPin(D7, HIGH);                                       // изсъхване
  runFor(1500000);
  CHECK(isSafetyLocked);                                  // все още в изчакване
  runFor(600000);
  CHECK(!isSafetyLocked);
  CHECK(printed("блокировката е свалена"));
  deliver(mkCmd(CMD_RELAY, 1, true, 1005));
  runFor(1000);
  CHECK(g_pin[5] == HIGH);

  // Трептене на изхода на компаратора: кратко "сухо" не сваля блокировката
  setPin(D7, LOW); runFor(1000);
  CHECK(isSafetyLocked && safetyTrips == 2 && g_pin[5] == LOW);
  for (int k = 0; k < 10; k++) { setPin(D7, HIGH); runFor(300000); setPin(D7, LOW); runFor(5000); }
  CHECK(isSafetyLocked && safetyTrips == 2);
  setPin(D7, HIGH); runFor(2100000);
  CHECK(!isSafetyLocked);

  // --- 6. Бутони: трептене и програмен филтър -----------------------------------
  uint32_t e0 = btnEdges[0];
  uint16_t pr0 = btnPresses[0];
  bool r0 = currentRelayState[0];
  int seqLevels[] = {LOW, HIGH, LOW, HIGH, LOW};          // натискане с 5 фронта
  for (int lv : seqLevels) { setPin(D5, lv); runFor(150); }
  runFor(100000);
  CHECK(btnPresses[0] == pr0 + 1);
  CHECK(currentRelayState[0] == !r0);
  int relLevels[] = {HIGH, LOW, HIGH};                    // отпускане с 3 фронта
  for (int lv : relLevels) { setPin(D5, lv); runFor(120); }
  runFor(100000);
  CHECK(btnEdges[0] == e0 + 8);
  CHECK(btnMaxEdges[0] == 5);
  CHECK(btnPresses[0] == pr0 + 1);

  // Кратък импулс (30 ms) не се приема като натискане
  uint16_t pr1 = btnPresses[1];
  setPin(D6, LOW); runFor(30000); setPin(D6, HIGH); runFor(100000);
  CHECK(btnPresses[1] == pr1);
  // Истинско натискане на D6 изключва всички релета
  deliver(mkCmd(CMD_RELAY, 2, true, 1006)); runFor(1000);
  CHECK(g_pin[4] == HIGH);
  setPin(D6, LOW); runFor(100000); setPin(D6, HIGH); runFor(100000);
  CHECK(btnPresses[1] == pr1 + 1);
  for (int i = 0; i < 4; i++) CHECK(g_pin[relayPins[i]] == LOW);
  runUntilMs(millis() + 2100);
  p = lastPacket();
  CHECK(p.btnPresses[0] == btnPresses[0] && p.btnEdges[0] == btnEdges[0] && p.btnMaxEdges[0] == 5);
  CHECK(p.btnPresses[1] == btnPresses[1] && p.btnEdges[1] == btnEdges[1]);

  // --- 7. PING и нулиране на статистиката ----------------------------------------
  before = g_sent.size();
  deliver(mkCmd(CMD_PING, 0, false, 1007));
  runFor(1000);
  CHECK(g_sent.size() == before + 1);
  p = lastPacket();
  CHECK(p.reportType == REPORT_COMMAND && p.ackSeq == 1007);

  deliver(mkCmd(CMD_RESET_STATS, 0, false, 1008));
  runFor(1000);
  p = lastPacket();
  CHECK(p.ackSeq == 1008 && p.btnEdges[0] == 0 && p.btnPresses[0] == 0 && p.cmdRejected == 0);
  CHECK(p.safetyTrips == 0 && p.safetyRefusals == 0 && p.loopMaxEverUs < 1000);
  CHECK(p.successPackets == 0 && p.failedPackets == 0);

  // --- 8. UART команди ----------------------------------------------------------
  Serial.in.push_back("R4 ON");  runFor(1000);  CHECK(g_pin[15] == HIGH);
  Serial.in.push_back("R4 OFF"); runFor(1000);  CHECK(g_pin[15] == LOW);
  Serial.in.push_back("ALL ON"); runFor(1000);
  for (int i = 0; i < 4; i++) CHECK(g_pin[relayPins[i]] == HIGH);
  runUntilMs(millis() + 2100);
  p = lastPacket();
  CHECK(NEAR(p.vcc, 470.0 / 1024.0 * 10.91 - 4 * 0.055, 1e-4));
  Serial.in.push_back("ALL OFF"); runFor(1000);
  for (int i = 0; i < 4; i++) CHECK(g_pin[relayPins[i]] == LOW);
  Serial.out.clear();
  Serial.in.push_back("SET VCCOFFSET 0.25"); runFor(1000);
  CHECK(cfg.vccOffset == 0.25f && NEAR(lastVcc, 470.0 / 1024.0 * 10.91 + 0.25, 1e-4));
  Serial.in.push_back("1"); runFor(1000);
  CHECK(printed("VCC            : 5.26 V"));              // вместо невалидното ESP.getVcc()
  Serial.in.push_back("6"); runFor(1000);
  CHECK(printed("--- ИЗМЕРВАНИЯ ---") && printed("Изпълнение ср./макс."));
  Serial.in.push_back("RESET STATS"); runFor(1000);
  CHECK(printed("Броячите за измерванията са нулирани"));
  Serial.in.push_back("SET VCCOFFSET 0"); runFor(1000);

  // --- 9. Грешка при четене на DHT11 --------------------------------------------
  DHT::fail = true;
  uint16_t de = dhtErrors;
  runUntilMs(millis() + 2100);
  CHECK(dhtErrors == de + 1);
  p = lastPacket();
  CHECK(p.dhtValid && NEAR(p.temperature, 22.9, 1e-4));   // задържа последната валидна стойност
  DHT::fail = false;

  // --- 10. Преминаване на micros() през нулата (на всеки ~71.6 min) ---------------
  g_us = (1ULL << 32) * 2 - 300000;                       // 0.3 s преди препълването
  resetStats();
  lastTelemetrySend = millis();                           // следващият пакет - след 2 s
  runFor(2200000);
  p = lastPacket();
  CHECK(p.reportType == REPORT_PERIODIC);
  CHECK(p.loopMaxUs >= 25500 && p.loopMaxUs < 30000);
  CHECK(p.periodMaxUs < 30000);
  CHECK(p.a0Sum == 4700);

  // --- 11. Интервал 1000 ms: DHT11 се чете през пакет -----------------------------
  struct_control c = mkCmd(CMD_CONFIG, 0, false, 1009);
  c.telemetryInterval = 1000; c.vccOffset = 0.10f; c.wifiChannel = 6;
  int commits = EEPROM.commits;
  deliver(c); runFor(1000);
  CHECK(cfg.telemetryInterval == 1000 && cfg.vccOffset == 0.10f && EEPROM.commits == commits + 1);
  CHECK(NEAR(lastVcc, 470.0 / 1024.0 * 10.91 + 0.10, 1e-4));
  p = lastPacket();
  CHECK(p.ackSeq == 1009 && p.cfgInterval == 1000);
  int reads0 = DHT::reads;
  size_t n0 = g_sent.size();
  runFor(4050000);
  int periodic = 0;
  for (size_t i = n0; i < g_sent.size(); i++) if (packet(i).reportType == REPORT_PERIODIC) periodic++;
  CHECK(periodic == 4);
  CHECK(DHT::reads - reads0 == 2);

  // --- 12. Смяна на канала -> запис и рестарт ------------------------------------
  c = mkCmd(CMD_CONFIG, 0, false, 1010);
  c.telemetryInterval = 1000; c.vccOffset = 0.10f; c.wifiChannel = 11;
  deliver(c);
  bool restarted = false;
  try { runFor(1000); } catch (RestartException &) { restarted = true; }
  CHECK(restarted);
  EEPROM.get(0, stored);
  CHECK(stored.wifiChannel == 11 && stored.version == 3);

  printf("\nSlave: %d проверки успешни, %d неуспешни\n", passes, fails);
  return fails ? 1 : 0;
}

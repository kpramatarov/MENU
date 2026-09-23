// ============================================================================
//  Тестове на логиката на главния модул (Master.ino) на персонален компютър.
//  JSON отговорите се записват във файлове за проверка с Python и Node.js.
// ============================================================================
#include "arduino_mock.h"
#include "../v3/Master/Master.ino"

static int passes = 0, fails = 0;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const uint8_t SLAVE_MAC[6] = {0xBC, 0xFF, 0x4D, 0x1D, 0xA5, 0xA6};
static std::string outDir = ".";

static void runFor(uint64_t us) {
  uint64_t end = g_us + us;
  while (g_us < end) { loop(); g_us += 20; }
}
static struct_message mkT(uint32_t id, uint32_t uptime, uint8_t type = REPORT_PERIODIC, uint32_t ack = 0) {
  struct_message m; memset(&m, 0, sizeof(m));
  m.protoVersion = PROTO_VERSION; m.fwVersion = FW_VERSION; m.reportType = type; m.cfgChannel = 6;
  m.readingId = id; m.ackSeq = ack; m.temperature = 22.9f; m.humidity = 34.0f;
  m.dhtValid = true; m.hr202State = true; m.cfgInterval = 2000; m.a0Sum = 4700;
  m.vcc = 5.01f; m.uptime = uptime; strcpy(m.resetReason, "External System");
  m.successPackets = id; m.loopCount = 42108; m.loopAvgUs = 3; m.loopMaxUs = 25593;
  m.periodMaxUs = 25613; m.windowMs = 1999; m.loopMaxEverUs = 25700;
  m.freeHeap = 49232; m.minFreeHeap = 48100; m.maxFreeBlock = 30000; m.heapFrag = 4;
  m.btnEdges[0] = 8; m.btnPresses[0] = 1; m.btnMaxEdges[0] = 5;
  return m;
}
static void rx(const struct_message &m) { g_recv_cb((uint8_t *)SLAVE_MAC, (uint8_t *)&m, sizeof(m)); }
static struct_control sentCtl(size_t i) {
  struct_control c; memcpy(&c, g_sent.at(i).data.data(), sizeof(c)); return c;
}
static void save(const char *name, const std::string &s) {
  std::string path = outDir + "/" + name;
  FILE *f = fopen(path.c_str(), "wb"); fwrite(s.data(), 1, s.size(), f); fclose(f);
}
static bool has(const std::string &s, const char *x) { return s.find(x) != std::string::npos; }
// Изчаква изпращане на пакет (до 1 s симулирано време)
static bool waitSend(size_t n) {
  for (int k = 0; k < 50000 && g_sent.size() <= n; k++) { loop(); g_us += 20; }
  return g_sent.size() > n;
}
// Изпълнява опашката докрай, като потвърждава всеки изпратен пакет
static void drainQueue() {
  size_t seen = g_sent.size();
  for (int k = 0; k < 400000 && (qCount > 0 || qState != Q_IDLE); k++) {
    loop(); g_us += 20;
    while (seen < g_sent.size()) { g_send_cb((uint8_t *)SLAVE_MAC, 0); seen++; }
  }
}

int main(int argc, char **argv) {
  if (argc > 1) outDir = argv[1];

  // --- 1. Миграция на конфигурацията от версия 2 --------------------------------
  Config v2; memset(&v2, 0, sizeof(v2));
  v2.magic = CFG_MAGIC; v2.version = 2;
  strcpy(v2.ssid, "TestNet"); strcpy(v2.pass, "secret123"); strcpy(v2.mdnsName, "smarthome");
  memcpy(v2.peerMac, SLAVE_MAC, 6); v2.wifiChannel = 6; v2.telemetryInterval = 2000;
  v2.vccOffset = 0.30f; strcpy(v2.otaPass, "esp8266ota"); strcpy(v2.webUser, "admin");
  strcpy(v2.webPass, "admin"); v2.linkTimeout = 10; v2.cmdSeq = 555;
  v2.crc = crc32((uint8_t *)&v2, sizeof(v2) - sizeof(v2.crc));
  EEPROM.put(0, v2);

  setup();
  CHECK(cfg.version == 3 && cfg.vccOffset == 0.0f);
  CHECK(strcmp(cfg.ssid, "TestNet") == 0 && strcmp(cfg.pass, "secret123") == 0);
  CHECK(cmdSeq == ESP.rnd);                               // случаен начален номер
  CHECK(has(Serial.out, "версия 2 е прехвърлена"));

  // --- 2. /data преди първия пакет -------------------------------------------------
  server.call("/data");
  CHECK(server.code == 200 && server.type == "application/json");
  CHECK(has(server.body, "\"online\":false") && has(server.body, "\"temperature\":null"));
  CHECK(has(server.body, "\"linkAlarm\":true") && has(server.body, "\"ackMin\":null"));
  save("data_empty.json", server.body);

  // --- 3. Прием: загубени, повторни, рестарт на подчинения ----------------------------
  rx(mkT(1, 5));
  CHECK(rxPackets == 1 && everReceived && lastT.readingId == 1 && rxLost == 0);
  rx(mkT(2, 7)); rx(mkT(5, 13));
  CHECK(rxPackets == 3 && rxLost == 2);
  rx(mkT(5, 13));
  CHECK(rxDup == 1 && rxPackets == 3);
  rx(mkT(1, 1));                                          // номерацията започва отначало
  CHECK(slaveReboots == 1 && rxLost == 2 && rxPackets == 4);
  rx(mkT(2, 3));
  rx(mkT(50, 1));                                         // рестарт, но с по-голям номер
  CHECK(slaveReboots == 2 && rxLost == 2);

  uint8_t old[92]; memset(old, 0, sizeof(old));
  const uint8_t OTHER_MAC[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const uint8_t SLAVE_AP_MAC[6] = {0xBE, 0xFF, 0x4D, 0x1D, 0xA5, 0xA6};   // MAC на softAP
  old[0] = 7;
  g_recv_cb((uint8_t *)OTHER_MAC, old, sizeof(old));      // чужд пакет - без предупреждение
  CHECK(rxBad == 1 && rxBadProto == 0);
  old[0] = 4;
  g_recv_cb((uint8_t *)SLAVE_AP_MAC, old, sizeof(old));   // подчиненият модул през softAP
  CHECK(rxBad == 2 && rxBadProto == 4);
  old[0] = 2;                                             // пакет от фърмуер v2
  g_recv_cb((uint8_t *)SLAVE_MAC, old, sizeof(old));
  CHECK(rxBad == 3 && rxBadProto == 2);
  server.call("/data");
  CHECK(has(server.body, "\"protoWarn\":2"));
  save("data_protowarn.json", server.body);

  // --- 4. /toggle и опашката --------------------------------------------------------
  size_t n0 = g_sent.size();
  server.call("/toggle", {{"id", "3"}});
  CHECK(server.code == 200 && server.body == "{\"state\":true}");
  CHECK(waitSend(n0));
  struct_control c = sentCtl(n0);
  CHECK(c.protoVersion == 3 && c.cmdType == CMD_RELAY && c.relayIndex == 3 && c.relayState);
  CHECK(c.seq == ESP.rnd + 1);
  g_us += 1800; g_send_cb((uint8_t *)SLAVE_MAC, 0);      // потвърждение след 1.8 ms
  CHECK(ackCount == 1 && ackMinUs >= 1800 && ackMaxUs < 2500);
  runFor(300000);
  CHECK(qCount == 0 && qState == Q_IDLE);

  server.call("/toggle", {{"id", "9"}});
  CHECK(server.code == 400 && has(server.body, "\"error\":"));
  server.call("/toggle");
  CHECK(server.code == 400);

  // PUSH по време на изпълнение на друга команда не прекъсва опашката
  n0 = g_sent.size();
  server.call("/toggle", {{"id", "1"}});
  Serial.in.push_back("PUSH");
  CHECK(waitSend(n0));
  CHECK(sentCtl(n0).cmdType == CMD_RELAY);
  runFor(10000);
  CHECK(g_sent.size() == n0 + 1);                          // чака потвърждението
  g_send_cb((uint8_t *)SLAVE_MAC, 0);
  CHECK(waitSend(n0 + 1));
  c = sentCtl(n0 + 1);
  CHECK(c.cmdType == CMD_CONFIG && c.vccOffset == 0.0f && c.telemetryInterval == 2000 && c.wifiChannel == 6);
  g_send_cb((uint8_t *)SLAVE_MAC, 0);
  runFor(300000);

  // Неуспешна доставка -> съобщение за грешка
  n0 = g_sent.size();
  server.call("/toggle", {{"id", "2"}});
  CHECK(waitSend(n0));
  g_send_cb((uint8_t *)SLAVE_MAC, 1);
  runFor(1000);
  CHECK(ackFail == 1 && has(lastErrorMsg, "не отговаря"));
  runFor(300000);

  // --- 5. Блокировка от влага ---------------------------------------------------
  struct_message t = mkT(51, 2); t.safetyLocked = true; t.hr202State = false;
  rx(t);
  server.call("/toggle", {{"id", "0"}});
  CHECK(server.code == 403);
  server.call("/onAll");
  CHECK(server.code == 403);
  server.call("/offAll");
  CHECK(server.code == 200 && server.body == "{\"queued\":4}");
  server.call("/data");
  CHECK(has(server.body, "\"locked\":true") && has(server.body, "\"hr202\":false"));
  save("data_locked.json", server.body);
  t = mkT(52, 4); t.safetyLocked = true;                   // сухо, но в изчакване
  rx(t);
  server.call("/toggle", {{"id", "0"}});
  CHECK(server.code == 403);
  rx(mkT(53, 6));
  n0 = g_sent.size();
  drainQueue();                                           // 4 x OFF
  CHECK(qCount == 0 && g_sent.size() == n0 + 4);
  for (size_t k = n0; k < n0 + 4; k++) CHECK(sentCtl(k).cmdType == CMD_RELAY && !sentCtl(k).relayState);

  // --- 6. Тест PING: 3 заявки, втората остава без отговор -----------------------------
  server.call("/ping", {{"n", "3"}});
  CHECK(server.code == 200 && pingRunning && pingTarget == 3);
  server.call("/ping", {{"n", "3"}});
  CHECK(server.code == 409);
  uint32_t id = 60;
  size_t idx = g_sent.size();
  for (int k = 0; k < 3; k++) {
    CHECK(waitSend(idx));
    c = sentCtl(idx); idx++;
    CHECK(c.cmdType == CMD_PING && c.seq == pingSeq && pingOutstanding);
    g_us += 1500; g_send_cb((uint8_t *)SLAVE_MAC, 0);    // MAC потвърждение
    if (k != 1) {
      g_us += 4000 + 1000 * k;                              // отчетът на подчинения модул
      rx(mkT(id++, 10, REPORT_COMMAND, c.seq));
      CHECK(!pingOutstanding);
      runFor(300000);                                       // следващата заявка след паузата
    } else {
      for (int j = 0; j < 100000 && pingLost == 0; j++) { loop(); g_us += 20; }
      CHECK(pingLost == 1);                                 // изтекли 500 ms без отговор
    }
  }
  runFor(10000);
  CHECK(!pingRunning && pingOk == 2 && pingLost == 1 && pingSent == 3);
  CHECK(rttMinUs >= 5500 && rttMinUs <= 5600 && rttMaxUs >= 7500 && rttMaxUs <= 7600);
  CHECK(has(Serial.out, "PING,1,") && has(Serial.out, "PING,2,LOST") && has(Serial.out, "PING,3,"));
  CHECK(has(Serial.out, "Край на теста"));
  server.call("/data");
  CHECK(has(server.body, "\"ping\":{\"run\":false,\"target\":3,\"sent\":3,\"ok\":2,\"lost\":1,\"min\":"));
  save("data_full.json", server.body);

  // --- 7. Време за цикъл на главния модул --------------------------------------------
  runFor(2100000);
  CHECK(mPubCount > 1000 && mPubWindowMs >= 2000 && mPubWindowMs <= 2010);
  CHECK(mPubAvgUs <= mPubMaxUs && mPubPeriodMaxUs >= mPubMaxUs);

  // --- 8. LOG ON: CSV ред за всеки периодичен пакет ------------------------------------
  Serial.out.clear();
  Serial.in.push_back("LOG ON");
  runFor(1000);
  CHECK(has(Serial.out, "LOG,t_ms,id,vcc"));
  Serial.out.clear();
  rx(mkT(id++, 20, REPORT_EVENT));
  runFor(1000);
  CHECK(!has(Serial.out, "LOG,"));                         // само периодичните пакети
  rx(mkT(id++, 22, REPORT_PERIODIC));
  runFor(1000);
  size_t pos = Serial.out.find("LOG,");
  CHECK(pos != std::string::npos);
  if (pos != std::string::npos) {
    std::string line = Serial.out.substr(pos, Serial.out.find('\n', pos) - pos);
    int fields = 1; for (char ch : line) if (ch == ',') fields++;
    CHECK(fields == 25);
    printf("CSV: %s\n", line.c_str());
  }

  // --- 9. RESET STATS ----------------------------------------------------------------
  n0 = g_sent.size();
  server.call("/resetStats");
  CHECK(server.code == 200 && rxPackets == 0 && rxLost == 0 && ackCount == 0 && pingOk == 0);
  CHECK(waitSend(n0) && sentCtl(n0).cmdType == CMD_RESET_STATS);
  drainQueue();

  // --- 10. UART: измервания, грешни аргументи, рестарт на подчинения ----------------------
  Serial.out.clear();
  Serial.in.push_back("7");           runFor(1000);
  CHECK(has(Serial.out, "--- ИЗМЕРВАНИЯ ---"));
  Serial.in.push_back("PING 0");      runFor(1000);
  Serial.in.push_back("PING 2000");   runFor(1000);
  CHECK(!pingRunning && has(Serial.out, "Допустим брой"));
  n0 = g_sent.size();
  Serial.in.push_back("RESET STATS");                    // не се бърка с R<n> ON/OFF
  drainQueue(); runFor(1000); drainQueue();
  CHECK(!has(Serial.out, "Формат: R1 ON"));
  CHECK(g_sent.size() == n0 + 1 && sentCtl(n0).cmdType == CMD_RESET_STATS);
  n0 = g_sent.size();
  Serial.in.push_back("5");
  CHECK(waitSend(n0) && sentCtl(n0).cmdType == CMD_RESTART);
  drainQueue();

  // --- 11. Екраниране на текст и невалидни числа --------------------------------------
  strcpy(cfg.ssid, "My \"Wi\\Fi\"\x01\t");
  server.call("/config");
  CHECK(server.code == 200 && has(server.body, "\"ssid\":\"My \\\"Wi\\\\Fi\\\"\\u0001\\u0009\""));
  save("config_escaped.json", server.body);
  strcpy(cfg.ssid, "VIVACOM_FiberNet_85C1");
  server.call("/config");
  save("config.json", server.body);

  t = mkT(id++, 30); t.temperature = NAN; t.humidity = INFINITY;
  rx(t);
  server.call("/data");
  CHECK(has(server.body, "\"temperature\":null") && has(server.body, "\"humidity\":null"));
  save("data_nan.json", server.body);

  // --- 12. Препълване на буфера -> 500 и валиден JSON с грешка --------------------------
  std::string big(3000, 'a');
  jsonBegin(); jsonStr("x", big.c_str()); jsonEnd(); sendJson(200);
  CHECK(server.code == 500 && jsonOverflowCount == 1 && has(server.body, "\"error\""));
  save("overflow.json", server.body);

  // Размер на най-големия отговор
  rx(mkT(id++, 40));
  server.call("/data");
  printf("/data: %zu B (буфер %d B)\n", server.body.size(), JSON_BUF_SIZE);
  CHECK(server.body.size() < JSON_BUF_SIZE * 3 / 4);

  save("index.html", index_html);

  printf("\nMaster: %d проверки успешни, %d неуспешни\n", passes, fails);
  return fails ? 1 : 0;
}

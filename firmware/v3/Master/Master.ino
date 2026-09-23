/*
 * ============================================================================
 *  СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС
 *  ГЛАВЕН МОДУЛ (MASTER / ХЪБ)
 *  ----------------------------------------------------------------------------
 *  Дипломна работа - ТУ София, ФЕТТ, катедра "Електронна техника"
 *  Дипломант: инж. Кръстиян Тодоров Праматаров, ф. № 901322003
 *  Ръководител: доц. д-р инж. Любомир Богданов
 *  ----------------------------------------------------------------------------
 *  Версия 3.0 - промени спрямо 2.0:
 *    - JSON отговорите на уеб сървъра се формират със snprintf в статичен
 *      буфер - без String и без фрагментиране на динамичната памет; текстовите
 *      полета се екранират, а препълването се проверява
 *    - Всички команди (реле, конфигурация, рестарт, тест) минават през
 *      неблокиращата опашка - PUSH и рестартът вече не я прекъсват
 *    - Измерване на времето до потвърждение на ESP-NOW (MAC ниво) и на
 *      двупосочното закъснение до подчинения модул (тест PING)
 *    - Броене на приетите и загубените пакети по поредните им номера
 *    - Време за цикъл на главния модул (micros()); показване на измерванията
 *      на двата модула в уеб интерфейса; запис на телеметрията като CSV по UART
 *    - Началният пореден номер на командите е случаен при всяко стартиране
 *    - Реле 4 на подчинения модул е на D8 (GPIO15)
 * ============================================================================
 */

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESP8266LLMNR.h>
#include <EEPROM.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
//  КРИПТИРАНЕ НА ESP-NOW
//  За да се включи: постави 1 ТУК И В SLAVE.INO и прекомпилирай ДВЕТЕ платки.
//  Ключовете трябва да съвпадат побайтово.
// ---------------------------------------------------------------------------
#define USE_ESPNOW_ENCRYPTION 0

#if USE_ESPNOW_ENCRYPTION
static uint8_t kokKey[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                              0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };
static uint8_t lmkKey[16] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                              0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
#endif

#define FW_VERSION    3
#define PROTO_VERSION 3

// ===========================================================================
//  СТРУКТУРИ НА ОБМЕНА  (ЗАДЪЛЖИТЕЛНО ИДЕНТИЧНИ В ДВАТА ФАЙЛА!)
// ===========================================================================

// Вид на отчета (телеметрията)
#define REPORT_PERIODIC 0   // периодичен пакет (на всеки cfgInterval)
#define REPORT_COMMAND  1   // отговор след изпълнена команда
#define REPORT_EVENT    2   // локално събитие: бутон, влага, UART

// Телеметрия: подчинен -> главен модул
typedef struct struct_message {
    uint8_t  protoVersion;
    uint8_t  fwVersion;
    uint8_t  reportType;       // REPORT_PERIODIC / REPORT_COMMAND / REPORT_EVENT
    uint8_t  cfgChannel;
    uint32_t readingId;        // пореден номер на пакета (от 1 след всеки рестарт)
    uint32_t ackSeq;           // пореден номер на последната изпълнена команда
    float    temperature;
    float    humidity;
    bool     dhtValid;         // има поне едно успешно четене от DHT11
    bool     hr202State;       // true - сухо, false - отчетена влага
    bool     relayState[4];
    uint16_t dhtErrors;        // брой неуспешни четения на DHT11
    uint16_t cfgInterval;      // потвърждение на приложената конфигурация
    uint16_t a0Sum;            // сума на пробите от A0 (0...10230) - за калибровка
    float    cfgVccOffset;
    float    vcc;              // V, след всички корекции
    uint32_t uptime;           // s
    char     resetReason[32];
    uint32_t successPackets;   // изпращания, потвърдени на MAC ниво
    uint32_t failedPackets;    // изпращания без потвърждение
    // --- време за цикъл, за прозореца между два периодични пакета ---
    uint32_t loopCount;        // брой итерации на loop()
    uint32_t loopAvgUs;        // средно време за изпълнение, µs
    uint32_t loopMaxUs;        // максимално време за изпълнение, µs
    uint32_t periodMaxUs;      // максимален интервал между началата на две итерации, µs
    uint32_t windowMs;         // продължителност на прозореца, ms
    uint32_t loopMaxEverUs;    // максимално време за изпълнение от стартирането, µs
    // --- динамична памет ---
    uint32_t freeHeap;         // B
    uint32_t minFreeHeap;      // най-малката стойност от стартирането, B
    uint32_t maxFreeBlock;     // най-големият непрекъснат свободен блок, B
    // --- бутони (индекс 0 - D5, индекс 1 - D6) ---
    uint32_t btnEdges[2];      // всички фронтове, отчетени по прекъсване
    uint16_t btnPresses[2];    // натискания, приети след филтъра
    uint16_t cmdRejected;      // отхвърлени команди (дубликат, чужд MAC, версия...)
    uint16_t safetyTrips;      // колко пъти е задействана защитата от влага
    uint16_t safetyRefusals;   // откази за включване по време на блокировка
    uint8_t  btnMaxEdges[2];   // най-много фронтове при едно превключване
    uint8_t  heapFrag;         // фрагментация на паметта, %
    bool     safetyLocked;     // блокировката от влага е активна
} struct_message;

// Типове команди: главен -> подчинен модул
#define CMD_RELAY       0
#define CMD_RESTART     1
#define CMD_CONFIG      2
#define CMD_PING        3   // заявка за незабавен отчет (измерване на закъснението)
#define CMD_RESET_STATS 4   // нулиране на броячите за измерванията

typedef struct struct_control {
    uint8_t  protoVersion;
    uint8_t  cmdType;
    uint8_t  relayIndex;
    bool     relayState;
    uint16_t telemetryInterval;
    float    vccOffset;
    uint8_t  wifiChannel;
    uint32_t seq;              // пореден номер срещу дублирани пакети
} struct_control;

static_assert(sizeof(struct_message) <= 250, "ESP-NOW: полезният товар е най-много 250 B");
static_assert(sizeof(struct_control) <= 250, "ESP-NOW: полезният товар е най-много 250 B");

struct_message incomingTelemetry;   // работно копие в callback-а
struct_message lastT;               // последният приет валиден пакет
struct_control outgoingControl;

// ===========================================================================
//  КОНФИГУРАЦИЯ В EEPROM
// ===========================================================================

#define CFG_MAGIC   0x484D4331UL   // "HMC1"
#define CFG_VERSION 3              // 3 - VCCOFFSET вече се прилага в подчинения модул
#define EEPROM_SIZE 512

typedef struct {
    uint32_t magic;
    uint8_t  version;
    char     ssid[33];
    char     pass[65];
    char     mdnsName[32];
    uint8_t  peerMac[6];
    uint8_t  wifiChannel;        // очакван канал на подчинения модул
    uint16_t telemetryInterval;  // ms
    float    vccOffset;          // V
    char     otaPass[33];
    char     webUser[17];
    char     webPass[33];
    uint8_t  webAuthEnabled;
    uint16_t linkTimeout;        // s - след колко мълчание се вдига авария
    uint32_t cmdSeq;             // от версия 3 не се използва (началният номер е случаен)
    uint32_t crc;
} Config;

Config cfg;

ESP8266WebServer server(80);

// --- Състояние на връзката с подчинения модул ---
unsigned long lastDataTime = 0;
bool     currentRelayState[4] = {false, false, false, false};
bool     everReceived = false;

// --- Прием: броячи по поредните номера на пакетите ---
uint32_t rxPackets = 0;          // приети валидни пакети
uint32_t rxLost = 0;             // загубени (пропуски в поредните номера)
uint32_t rxDup = 0;              // повторно приети
uint32_t rxBad = 0;              // отхвърлени (дължина, версия)
uint16_t slaveReboots = 0;       // забелязани рестарти на подчинения модул
uint32_t lastReadingId = 0;
bool     haveReadingId = false;
volatile uint8_t rxBadProto = 0; // версия на протокола в последния отхвърлен пакет
unsigned long rxBadTime = 0;
volatile bool logPending = false;
bool     logEnabled = false;     // CSV запис на телеметрията по UART (LOG ON)

// --- Състояние на изпращането ---
volatile bool waitingForDelivery = false;
volatile bool deliverySuccess = false;
uint32_t cmdSeq = 0;             // пореден номер на командите (случаен старт)

// --- Време до потвърждение на ESP-NOW (MAC ниво): esp_now_send -> OnDataSent ---
volatile uint32_t txStartUs = 0;
uint32_t ackCount = 0, ackFail = 0;
uint32_t ackMinUs = 0xFFFFFFFFUL, ackMaxUs = 0;
uint64_t ackSumUs = 0;

// --- Тест PING: двупосочно закъснение до подчинения модул ---
#define PING_TIMEOUT_MS 500
#define PING_MAX_COUNT  1000
bool     pingRunning = false;
uint16_t pingTarget = 0, pingSent = 0, pingOk = 0, pingLost = 0;
volatile bool     pingOutstanding = false;
volatile uint32_t pingSeq = 0, pingT0Us = 0;
volatile uint32_t pingLastRttUs = 0;
volatile bool     pingResultPending = false;   // за ред в CSV от loop()
unsigned long pingSentMs = 0;
uint32_t rttMinUs = 0xFFFFFFFFUL, rttMaxUs = 0;
uint64_t rttSumUs = 0;

// --- Време за цикъл на главния модул (прозорец 2 s) ---
#define MSTATS_WINDOW_MS 2000
uint32_t mCount = 0, mMaxUs = 0, mPeriodMaxUs = 0;
uint64_t mSumUs = 0;
uint32_t mWindowStartMs = 0, mPrevStartUs = 0;
bool     mHavePrev = false;
uint32_t mLoopMaxEverUs = 0;
uint32_t mPubCount = 0, mPubAvgUs = 0, mPubMaxUs = 0, mPubPeriodMaxUs = 0, mPubWindowMs = 0;
uint32_t mMinFreeHeap = 0xFFFFFFFFUL;

char     lastErrorMsg[96] = "";
unsigned long lastErrorTime = 0;

void setError(const char *msg) {
  strncpy(lastErrorMsg, msg, sizeof(lastErrorMsg) - 1);
  lastErrorMsg[sizeof(lastErrorMsg) - 1] = '\0';
  lastErrorTime = millis();
}

// ===========================================================================
//  НЕБЛОКИРАЩА ОПАШКА ОТ КОМАНДИ
//  Премахва блокиращия цикъл "while (waitingForDelivery)" от обработчиците.
// ===========================================================================
#define QUEUE_SIZE 12
#define ACK_TIMEOUT_MS 500
#define CMD_SPACING_MS 250

typedef struct { uint8_t type; uint8_t relayIndex; bool state; } QueueItem;
QueueItem cmdQueue[QUEUE_SIZE];
uint8_t qHead = 0, qCount = 0;

enum QState { Q_IDLE, Q_WAIT_ACK, Q_COOLDOWN };
QState qState = Q_IDLE;
unsigned long qSendTime = 0, qCooldownStart = 0;

bool enqueueCmd(uint8_t type, uint8_t idx, bool state) {
  if (qCount >= QUEUE_SIZE) { setError("Опашката е препълнена"); return false; }
  uint8_t pos = (qHead + qCount) % QUEUE_SIZE;
  cmdQueue[pos].type       = type;
  cmdQueue[pos].relayIndex = idx;
  cmdQueue[pos].state      = state;
  qCount++;
  return true;
}

bool enqueueRelay(uint8_t idx, bool state) { return enqueueCmd(CMD_RELAY, idx, state); }

void sendControl(uint8_t type, uint8_t idx, bool state) {
  outgoingControl.protoVersion      = PROTO_VERSION;
  outgoingControl.cmdType           = type;
  outgoingControl.relayIndex        = idx;
  outgoingControl.relayState        = state;
  outgoingControl.telemetryInterval = cfg.telemetryInterval;
  outgoingControl.vccOffset         = cfg.vccOffset;
  outgoingControl.wifiChannel       = cfg.wifiChannel;
  outgoingControl.seq               = ++cmdSeq;
  waitingForDelivery = true;
  deliverySuccess = false;
  if (type == CMD_PING) {                     // начало на измерването на RTT
    pingSeq = cmdSeq;
    pingSentMs = millis();
    pingOutstanding = true;
    pingSent++;
    pingT0Us = micros();
  }
  txStartUs = micros();
  esp_now_send(cfg.peerMac, (uint8_t *)&outgoingControl, sizeof(outgoingControl));
}

void processQueue() {
  switch (qState) {
    case Q_IDLE:
      if (qCount > 0) {
        sendControl(cmdQueue[qHead].type, cmdQueue[qHead].relayIndex, cmdQueue[qHead].state);
        qSendTime = millis();
        qState = Q_WAIT_ACK;
      }
      break;

    case Q_WAIT_ACK:
      // Реалното състояние на релетата идва с отчета на подчинения модул
      // веднага след изпълнението - тук се следи само доставката.
      if (!waitingForDelivery) {                      // получено потвърждение
        if (!deliverySuccess) setError("Подчиненият модул не отговаря");
        qHead = (qHead + 1) % QUEUE_SIZE; qCount--;
        qCooldownStart = millis(); qState = Q_COOLDOWN;
      } else if (millis() - qSendTime > ACK_TIMEOUT_MS) {
        waitingForDelivery = false;
        setError("Изтекло време за потвърждение");
        qHead = (qHead + 1) % QUEUE_SIZE; qCount--;
        qCooldownStart = millis(); qState = Q_COOLDOWN;
      }
      break;

    case Q_COOLDOWN:
      if (millis() - qCooldownStart >= CMD_SPACING_MS) qState = Q_IDLE;
      break;
  }
}

// ===========================================================================
//  ТЕСТ PING - последователни заявки CMD_PING; RTT = от изпращането до
//  пристигането на отчета с номера на заявката (измерва се с micros()).
// ===========================================================================

bool startPingTest(long n) {
  if (pingRunning || n < 1 || n > PING_MAX_COUNT) return false;
  pingTarget = (uint16_t)n;
  pingSent = 0; pingOk = 0; pingLost = 0;
  rttMinUs = 0xFFFFFFFFUL; rttMaxUs = 0; rttSumUs = 0;
  pingOutstanding = false;
  pingRunning = true;
  Serial.printf_P(PSTR("\n📶 Тест на закъснението: %u заявки (CSV: PING,<№>,<RTT в µs>)\n"), pingTarget);
  return true;
}

void printPingSummary() {
  Serial.printf_P(PSTR("📶 Край на теста: изпратени %u, успешни %u, загубени %u\n"),
                  pingSent, pingOk, pingLost);
  if (pingOk > 0) {
    Serial.printf_P(PSTR("   RTT мин / ср / макс: %lu / %lu / %lu µs\n"),
                    (unsigned long)rttMinUs, (unsigned long)(rttSumUs / pingOk),
                    (unsigned long)rttMaxUs);
  }
}

void processPingTest() {
  if (pingResultPending) {                       // ред за всяка успешна заявка
    pingResultPending = false;                   // (следващата още не е изпратена)
    Serial.printf_P(PSTR("PING,%u,%lu\n"), pingSent, (unsigned long)pingLastRttUs);
  }
  if (!pingRunning) return;
  if (pingOutstanding) {
    if (millis() - pingSentMs <= PING_TIMEOUT_MS) return;
    pingOutstanding = false;                     // няма отговор в срок
    pingLost++;
    Serial.printf_P(PSTR("PING,%u,LOST\n"), pingSent);
  }
  if (pingSent >= pingTarget) {
    pingRunning = false;
    printPingSummary();
    return;
  }
  if (qCount == 0 && qState == Q_IDLE) enqueueCmd(CMD_PING, 0, false);
}

// ===========================================================================
//  РАБОТА С EEPROM
// ===========================================================================

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic   = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  strncpy(cfg.ssid,     "ВЪВЕДЕТЕ_SSID",   sizeof(cfg.ssid) - 1);
  strncpy(cfg.pass,     "ВЪВЕДЕТЕ_ПАРОЛА", sizeof(cfg.pass) - 1);
  strncpy(cfg.mdnsName, "smarthome",       sizeof(cfg.mdnsName) - 1);
  uint8_t defMac[6] = {0xBC, 0xFF, 0x4D, 0x1D, 0xA5, 0xA6};
  memcpy(cfg.peerMac, defMac, 6);
  cfg.wifiChannel       = 6;
  cfg.telemetryInterval = 2000;
  cfg.vccOffset         = 0.0;
  strncpy(cfg.otaPass, "esp8266ota", sizeof(cfg.otaPass) - 1);
  strncpy(cfg.webUser, "admin",      sizeof(cfg.webUser) - 1);
  strncpy(cfg.webPass, "admin",      sizeof(cfg.webPass) - 1);
  cfg.webAuthEnabled = 0;      // изключена по подразбиране за улеснение при тест
  cfg.linkTimeout    = 10;
  cfg.cmdSeq         = 0;
}

void saveConfig() {
  cfg.crc = crc32((uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  EEPROM.put(0, cfg);
  if (EEPROM.commit()) Serial.println(F("💾 Конфигурацията е записана в EEPROM."));
  else                 Serial.println(F("❌ Грешка при запис в EEPROM!"));
}

// Гарантира завършваща нула във всички текстови полета
void terminateStrings() {
  cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
  cfg.pass[sizeof(cfg.pass) - 1] = '\0';
  cfg.mdnsName[sizeof(cfg.mdnsName) - 1] = '\0';
  cfg.otaPass[sizeof(cfg.otaPass) - 1] = '\0';
  cfg.webUser[sizeof(cfg.webUser) - 1] = '\0';
  cfg.webPass[sizeof(cfg.webPass) - 1] = '\0';
}

void loadConfig() {
  EEPROM.get(0, cfg);
  uint32_t calc = crc32((uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  bool intact = (cfg.magic == CFG_MAGIC && cfg.crc == calc);

  if (intact && cfg.version == CFG_VERSION) {
    terminateStrings();
    Serial.println(F("✅ Конфигурацията е прочетена от EEPROM."));
  } else if (intact && cfg.version == 2) {
    // Разположението на полетата е същото като във версия 2. Там VCCOFFSET не
    // се прилагаше от подчинения модул (ефективно е бил 0 V), затова се записва
    // 0 - иначе първото PUSH би изместило калибрираното показание.
    terminateStrings();
    cfg.version   = CFG_VERSION;
    cfg.vccOffset = 0.0;
    Serial.println(F("✅ Конфигурацията от версия 2 е прехвърлена (VCCOFFSET = 0.00 V)."));
    saveConfig();
  } else {
    Serial.println(F("⚠️ Няма валидна конфигурация - зареждам фабричните настройки."));
    setDefaults();
    saveConfig();
  }
}

void macToStr(const uint8_t *mac, char *out) {   // out: поне 18 байта
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool parseMac(const String &s, uint8_t *out) {
  unsigned int b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
    return false;
  for (int i = 0; i < 6; i++) { if (b[i] > 255) return false; out[i] = (uint8_t)b[i]; }
  return true;
}

// Подчиненият модул е в защитен режим (влага или изчакване след изсъхване)
bool slaveLocked() {
  return everReceived && (lastT.safetyLocked || !lastT.hr202State);
}

// ===========================================================================
//  JSON СЪС snprintf В СТАТИЧЕН БУФЕР
//  Отговорите се формират без String: няма заделяне и освобождаване на
//  динамична памет при всяка заявка (браузърът пита /data всяка секунда).
// ===========================================================================
#define JSON_BUF_SIZE 2048
static char   jsonBuf[JSON_BUF_SIZE];
static size_t jsonLen = 0;
static bool   jsonOverflow = false;    // отговорът не се е побрал в буфера
static bool   jsonFirst = true;        // следващият елемент е първи в обекта
uint32_t      jsonOverflowCount = 0;

// Добавя n байта, като винаги оставя място за завършващата нула
static void jsonPut(const char *s, size_t n) {
  if (jsonOverflow) return;
  if (jsonLen + n >= JSON_BUF_SIZE) { jsonOverflow = true; return; }
  memcpy(jsonBuf + jsonLen, s, n);
  jsonLen += n;
  jsonBuf[jsonLen] = '\0';
}

// Добавя форматиран текст (vsnprintf); при съкращаване отбелязва препълване.
// Атрибутът format кара компилатора да проверява аргументите като при printf.
__attribute__((format(printf, 1, 2)))
static void jsonPrintf(const char *fmt, ...) {
  if (jsonOverflow) return;
  size_t room = JSON_BUF_SIZE - jsonLen;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(jsonBuf + jsonLen, room, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= room) { jsonOverflow = true; jsonBuf[jsonLen] = '\0'; return; }
  jsonLen += (size_t)n;
}

static void jsonBegin() {
  jsonLen = 0; jsonOverflow = false; jsonFirst = true;
  jsonBuf[0] = '\0';
  jsonPut("{", 1);
}
static void jsonEnd() { jsonPut("}", 1); }

static void jsonKey(const char *key) {
  if (!jsonFirst) jsonPut(",", 1);
  jsonFirst = false;
  jsonPrintf("\"%s\":", key);
}
static void jsonObjBegin(const char *key) { jsonKey(key); jsonPut("{", 1); jsonFirst = true; }
static void jsonObjEnd()                  { jsonPut("}", 1); jsonFirst = false; }

static void jsonU32(const char *key, uint32_t v) { jsonKey(key); jsonPrintf("%lu", (unsigned long)v); }
static void jsonI32(const char *key, int32_t v)  { jsonKey(key); jsonPrintf("%ld", (long)v); }
static void jsonBool(const char *key, bool v)    { jsonKey(key); jsonPrintf("%s", v ? "true" : "false"); }
static void jsonNull(const char *key)            { jsonKey(key); jsonPut("null", 4); }

static void jsonFloat(const char *key, float v, uint8_t decimals, bool valid = true) {
  jsonKey(key);
  if (!valid || isnan(v) || isinf(v)) { jsonPut("null", 4); return; }
  jsonPrintf("%.*f", (int)decimals, (double)v);
}

// Текст в кавички, екраниран по RFC 8259: ", \ и управляващите символи.
// Байтовете на UTF-8 (кирилицата) се предават без промяна.
static void jsonStr(const char *key, const char *s) {
  jsonKey(key);
  jsonPut("\"", 1);
  for (const unsigned char *p = (const unsigned char *)s; *p && !jsonOverflow; p++) {
    if (*p == '"' || *p == '\\') { char e[2] = { '\\', (char)*p }; jsonPut(e, 2); }
    else if (*p < 0x20)          { jsonPrintf("\\u%04x", (unsigned)*p); }
    else                         { jsonPut((const char *)p, 1); }
  }
  jsonPut("\"", 1);
}

static void sendJson(int code) {
  if (jsonOverflow) {
    jsonOverflowCount++;
    server.send(500, "application/json", "{\"error\":\"Отговорът не се събира в буфера\"}");
    return;
  }
  server.send(code, "application/json", jsonBuf, jsonLen);
}

static void sendJsonError(int code, const char *msg) {
  jsonBegin();
  jsonStr("error", msg);
  jsonEnd();
  sendJson(code);
}

// ===========================================================================
//  УЕБ ИНТЕРФЕЙС
// ===========================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="bg">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Управление на Умен Дом</title>
  <style>
    :root { --bg: #121212; --card: #1e1e1e; --text: #ffffff; }
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; }
    h1 { text-align: center; color: #00d2ff; font-weight: 300; margin-bottom: 20px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; max-width: 900px; margin: 0 auto; }
    .card { background: var(--card); padding: 25px; border-radius: 12px; text-align: center; box-shadow: 0 8px 15px rgba(0,0,0,0.3); border-top: 4px solid; }
    .card.temp { border-color: #ff5722; }
    .card.hum { border-color: #00bcd4; }
    .card.sensor { border-color: #8bc34a; }
    .card.relay-card { border-color: #e91e63; }
    .master-controls { grid-column: 1 / -1; display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
    .card.master-on { border-color: #2196F3; }
    .card.master-off { border-color: #f44336; }
    .card.diag-card { border-color: #9c27b0; grid-column: 1 / -1; background: #1a1a24; }
    .card.meas-card { border-color: #ffc107; grid-column: 1 / -1; background: #1c1a14; }
    .card.cfg-card { border-color: #607d8b; grid-column: 1 / -1; background: #16191c; }
    .diag-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin-top: 15px; }
    .diag-item { background: rgba(255,255,255,0.05); padding: 15px; border-radius: 8px; font-size: 1.1rem; }
    .diag-label { color: #aaa; font-size: 0.85rem; display: block; text-transform: uppercase; margin-bottom: 5px; }
    .diag-val { font-weight: bold; color: #00d2ff; word-break: break-all; }
    .meas-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 10px; }
    .title { color: #aaa; font-size: 1.1rem; margin-bottom: 15px; text-transform: uppercase; letter-spacing: 1px; }
    .value { font-size: 2.8rem; font-weight: bold; margin: 10px 0; }
    .unit { font-size: 1.2rem; color: #888; }
    .status-text { font-size: 1.8rem; font-weight: bold; margin: 15px 0; }
    .footer { text-align: center; margin-top: 40px; color: #666; font-size: 0.95rem; }
    button { color: white; border: none; padding: 15px 10px; font-size: 1.1rem; font-weight: bold; border-radius: 8px; cursor: pointer; width: 100%; transition: 0.3s; margin-top: 10px;}
    button:active { transform: scale(0.95); }
    button.action-on { background-color: #4CAF50; }
    button.action-off { background-color: #f44336; }
    button.danger { background-color: #d32f2f; padding: 20px 10px; font-size: 1.3rem;}
    button.primary { background-color: #1976D2; padding: 20px 10px; font-size: 1.3rem;}
    button.neutral { background-color: #546e7a; padding: 20px 10px; font-size: 1.3rem;}
    #banner { max-width: 900px; margin: 0 auto 20px auto; padding: 15px; border-radius: 10px; text-align: center; font-weight: bold; display: none; }
    #banner.alarm { display: block; background: #4a1010; border: 2px solid #f44336; color: #ff8a80; }
    #banner.err   { display: block; background: #4a3a10; border: 2px solid #ff9800; color: #ffcc80; }
  </style>
</head>
<body>
  <h1>Управление на Умен Дом</h1>
  <div id="banner"></div>
  <div class="grid">
    <div class="card temp"><div class="title">🌡️ Температура</div><div class="value"><span id="temp">--</span><span class="unit"> °C</span></div></div>
    <div class="card hum"><div class="title">💧 Влажност</div><div class="value"><span id="hum">--</span><span class="unit"> %</span></div></div>
    <div class="card sensor"><div class="title">💦 Сензор за влага</div><div class="status-text" id="hr202">--</div></div>

    <div class="card relay-card"><div class="title">🔌 Реле 1 (D0)</div><button id="relayBtn0" class="action-on" onclick="toggleRelay(0)">ВКЛЮЧИ</button></div>
    <div class="card relay-card"><div class="title">🔌 Реле 2 (D1)</div><button id="relayBtn1" class="action-on" onclick="toggleRelay(1)">ВКЛЮЧИ</button></div>
    <div class="card relay-card"><div class="title">🔌 Реле 3 (D2)</div><button id="relayBtn2" class="action-on" onclick="toggleRelay(2)">ВКЛЮЧИ</button></div>
    <div class="card relay-card"><div class="title">🔌 Реле 4 (D8)</div><button id="relayBtn3" class="action-on" onclick="toggleRelay(3)">ВКЛЮЧИ</button></div>

    <div class="master-controls">
      <div class="card master-on"><div class="title">⚡ Общ старт</div><button class="primary" onclick="turnOnAll()">ПУСНИ ВСИЧКИ</button></div>
      <div class="card master-off"><div class="title">⚠️ Аварийен стоп</div><button class="danger" onclick="turnOffAll()">СПРИ ВСИЧКИ</button></div>
    </div>

    <div class="card diag-card">
      <div class="title">📊 Хардуерна и софтуерна диагностика</div>
      <div class="diag-grid">
        <div class="diag-item"><span class="diag-label">Качество на връзката</span><span class="diag-val" id="d-health">--</span> %</div>
        <div class="diag-item"><span class="diag-label">Загубени пакети</span><span class="diag-val" id="d-fail" style="color:#f44336;">--</span></div>
        <div class="diag-item"><span class="diag-label">Свободна памет (RAM)</span><span class="diag-val" id="d-ram">--</span> B</div>
        <div class="diag-item"><span class="diag-label">Напрежение (VCC)</span><span class="diag-val" id="d-vcc">--</span> V</div>
        <div class="diag-item"><span class="diag-label">Време за цикъл: ср. / макс.</span><span class="diag-val" id="d-cpu">--</span></div>
        <div class="diag-item"><span class="diag-label">Време на работа</span><span class="diag-val" id="d-upt">--</span> s</div>
        <div class="diag-item"><span class="diag-label">Причина за рестарт</span><span class="diag-val" id="d-rst">--</span></div>
        <div class="diag-item"><span class="diag-label">Грешки от DHT11</span><span class="diag-val" id="d-dht">--</span></div>
        <div class="diag-item"><span class="diag-label">Команди в опашка</span><span class="diag-val" id="d-que">--</span></div>
      </div>
    </div>

    <div class="card meas-card">
      <div class="title">⏱️ Измервания</div>
      <div class="diag-grid">
        <div class="diag-item"><span class="diag-label">Цикъл подчинен: макс. от старта</span><span class="diag-val" id="s-loopmax">--</span></div>
        <div class="diag-item"><span class="diag-label">Итерации за 1 s (подчинен)</span><span class="diag-val" id="s-freq">--</span></div>
        <div class="diag-item"><span class="diag-label">Макс. период (подчинен)</span><span class="diag-val" id="s-period">--</span></div>
        <div class="diag-item"><span class="diag-label">Цикъл главен: ср. / макс.</span><span class="diag-val" id="m-loop">--</span></div>
        <div class="diag-item"><span class="diag-label">Итерации за 1 s (главен)</span><span class="diag-val" id="m-freq">--</span></div>
        <div class="diag-item"><span class="diag-label">Памет подчинен: мин. / блок / фрагм.</span><span class="diag-val" id="s-heap">--</span></div>
        <div class="diag-item"><span class="diag-label">Памет главен: своб. / мин. / фрагм.</span><span class="diag-val" id="m-heap">--</span></div>
        <div class="diag-item"><span class="diag-label">ESP-NOW потвърждение: мин. / ср. / макс.</span><span class="diag-val" id="ack">--</span></div>
        <div class="diag-item"><span class="diag-label">Приети / загубени / доставени</span><span class="diag-val" id="rx">--</span></div>
        <div class="diag-item"><span class="diag-label">PING (RTT): мин. / ср. / макс.</span><span class="diag-val" id="rtt">--</span></div>
        <div class="diag-item"><span class="diag-label">PING: успешни / загубени</span><span class="diag-val" id="ping">--</span></div>
        <div class="diag-item"><span class="diag-label">Бутон D5: натиск. / фронтове / макс.</span><span class="diag-val" id="b1">--</span></div>
        <div class="diag-item"><span class="diag-label">Бутон D6: натиск. / фронтове / макс.</span><span class="diag-val" id="b2">--</span></div>
        <div class="diag-item"><span class="diag-label">Защита: задействания / откази</span><span class="diag-val" id="safety">--</span></div>
        <div class="diag-item"><span class="diag-label">A0 (средно от 10 проби)</span><span class="diag-val" id="a0">--</span></div>
        <div class="diag-item"><span class="diag-label">Отхвърлени команди</span><span class="diag-val" id="rej">--</span></div>
      </div>
      <div class="meas-actions">
        <button class="primary" onclick="startPing()">ТЕСТ НА ЗАКЪСНЕНИЕТО (100)</button>
        <button class="neutral" onclick="resetStats()">НУЛИРАЙ СТАТИСТИКАТА</button>
      </div>
    </div>

    <div class="card cfg-card">
      <div class="title">⚙️ Текуща конфигурация (променя се по UART)</div>
      <div class="diag-grid">
        <div class="diag-item"><span class="diag-label">Мрежа (SSID)</span><span class="diag-val" id="c-ssid">--</span></div>
        <div class="diag-item"><span class="diag-label">Локално име</span><span class="diag-val" id="c-name">--</span></div>
        <div class="diag-item"><span class="diag-label">IP адрес</span><span class="diag-val" id="c-ip">--</span></div>
        <div class="diag-item"><span class="diag-label">Канал (хъб / модул)</span><span class="diag-val" id="c-ch">--</span></div>
        <div class="diag-item"><span class="diag-label">MAC на модула</span><span class="diag-val" id="c-mac">--</span></div>
        <div class="diag-item"><span class="diag-label">Интервал телеметрия</span><span class="diag-val" id="c-int">--</span> ms</div>
        <div class="diag-item"><span class="diag-label">Корекция на VCC</span><span class="diag-val" id="c-off">--</span> V</div>
        <div class="diag-item"><span class="diag-label">Версия фърмуер</span><span class="diag-val" id="c-fw">--</span></div>
      </div>
    </div>
  </div>
  <div class="footer">Статус: <span id="status" style="font-weight:bold;">Чакане на данни...</span> | Пакет №: <span id="pkt">--</span></div>

  <script>
    let safetyLocked = false;

    function el(id) { return document.getElementById(id); }
    function setText(id, v) { const e = el(id); if (e) e.innerText = v; }
    function num(v, d) { return (v === null || v === undefined) ? '--' : Number(v).toFixed(d); }
    function us(v) {                       // µs до 1 ms, след това ms
      if (v === null || v === undefined) return '--';
      return v < 1000 ? v + ' µs' : (v / 1000).toFixed(2) + ' ms';
    }
    function perSec(count, windowMs) { return windowMs > 0 ? Math.round(count * 1000 / windowMs) : '--'; }

    function toggleRelay(idx) {
      const btn = el('relayBtn' + idx);
      const isCurrentlyOn = btn.classList.contains('action-off');
      if (safetyLocked && !isCurrentlyOn) { alert("⚠️ ВНИМАНИЕ: Отчетена е влага!"); return; }
      fetch('/toggle?id=' + idx).then(r=>r.json()).then(d => {
        if (d.error) { alert("📡 " + d.error); return; }
        updateButtonUI(idx, d.state);
      });
    }
    function turnOnAll() {
      if (safetyLocked) { alert("⚠️ ВНИМАНИЕ: Отчетена е влага!"); return; }
      fetch('/onAll').then(r=>r.json()).then(d => { if (d.error) alert("📡 " + d.error); });
    }
    function turnOffAll() {
      fetch('/offAll').then(r=>r.json()).then(d => { if (d.error) alert("📡 " + d.error); });
    }
    function startPing() {
      fetch('/ping?n=100').then(r=>r.json()).then(d => { if (d.error) alert("📡 " + d.error); });
    }
    function resetStats() {
      if (!confirm('Да се нулират ли броячите за измерванията?')) return;
      fetch('/resetStats').then(r=>r.json()).then(d => { if (d.error) alert("📡 " + d.error); });
    }
    function updateButtonUI(idx, state) {
      const btn = el('relayBtn' + idx);
      if (state) { btn.innerText = 'ИЗКЛЮЧИ'; btn.classList.remove('action-on'); btn.classList.add('action-off'); }
      else       { btn.innerText = 'ВКЛЮЧИ';  btn.classList.remove('action-off'); btn.classList.add('action-on'); }
    }

    function showBanner(text, kind) {
      const b = el('banner');
      if (!text) { b.className = ''; b.style.display = 'none'; return; }
      b.innerText = text; b.className = kind; b.style.display = '';
    }

    fetch('/config').then(r=>r.json()).then(c => {
      setText('c-ssid', c.ssid);
      setText('c-name', c.name + '.local');
      setText('c-ip',   c.ip);
      setText('c-mac',  c.mac);
      setText('c-int',  c.interval);
      setText('c-off',  num(c.vccOffset, 2));
      setText('c-ch',   c.hubChannel + ' / ' + c.slaveChannel);
      setText('c-fw',   'M' + c.fwMaster);
    });

    setInterval(function() {
      fetch('/data').then(response => response.json()).then(d => {
          setText('temp', num(d.temperature, 2));
          setText('hum',  num(d.humidity, 2));
          setText('pkt',  d.online ? d.id : '--');

          const hr = el('hr202');
          if (!d.online)     { hr.innerText = '--'; hr.style.color = '#aaa'; safetyLocked = false; }
          else if (!d.hr202) { hr.innerText = 'МОКРО (БЛОКИРАНО)'; hr.style.color = '#2196F3'; safetyLocked = true; }
          else if (d.locked) { hr.innerText = 'СУХО (ИЗЧАКВАНЕ)';  hr.style.color = '#ff9800'; safetyLocked = true; }
          else               { hr.innerText = 'СУХО'; hr.style.color = '#8bc34a'; safetyLocked = false; }

          d.relays.forEach((state, idx) => updateButtonUI(idx, state));

          const total = d.success + d.failed;
          setText('d-health', total > 0 ? ((d.success / total) * 100).toFixed(1) : '100.0');
          setText('d-fail', d.failed);
          setText('d-ram',  d.freeHeap);
          setText('d-vcc',  num(d.vcc, 2));
          setText('d-cpu',  us(d.s.loopAvg) + ' / ' + us(d.s.loopMax));
          setText('d-upt',  d.uptime);
          setText('d-rst',  d.resetReason);
          setText('d-dht',  d.dhtErrors);
          setText('d-que',  d.queue);
          setText('c-fw',   'M' + d.fwMaster + ' / S' + d.fwSlave);

          const s = d.s, m = d.m, l = d.link, p = d.ping;
          setText('s-loopmax', us(s.loopMaxEver));
          setText('s-freq',    perSec(s.loopCount, s.window));
          setText('s-period',  us(s.periodMax));
          setText('m-loop',    us(m.loopAvg) + ' / ' + us(m.loopMax));
          setText('m-freq',    perSec(m.loopCount, m.window));
          setText('s-heap',    s.minHeap + ' B / ' + s.maxBlock + ' B / ' + s.frag + ' %');
          setText('m-heap',    m.heap + ' B / ' + m.minHeap + ' B / ' + m.frag + ' %');
          setText('ack',       us(l.ackMin) + ' / ' + us(l.ackAvg) + ' / ' + us(l.ackMax) +
                               (l.ackFail > 0 ? ' (' + l.ackFail + ' неусп.)' : ''));
          const rxTotal = l.rx + l.lost;
          setText('rx',        l.rx + ' / ' + l.lost + ' / ' +
                               (rxTotal > 0 ? ((l.rx / rxTotal) * 100).toFixed(2) + ' %' : '--'));
          setText('rtt',       us(p.min) + ' / ' + us(p.avg) + ' / ' + us(p.max));
          setText('ping',      p.ok + ' / ' + p.lost + (p.run ? ' (' + p.sent + ' от ' + p.target + ')' : ''));
          setText('b1',        s.b1p + ' / ' + s.b1e + ' / ' + s.b1m);
          setText('b2',        s.b2p + ' / ' + s.b2e + ' / ' + s.b2m);
          setText('safety',    s.trips + ' / ' + s.refusals);
          setText('a0',        num(d.a0, 1));
          setText('rej',       s.rejected);

          if (d.protoWarn) {
            showBanner('⚠️ Подчиненият модул използва протокол v' + d.protoWarn +
                       ', а главният - v' + d.proto + '. Обновете и двата модула.', 'err');
          } else if (d.linkAlarm) {
            showBanner('⛔ НЯМА ВРЪЗКА С ПОДЧИНЕНИЯ МОДУЛ (' + d.timeSince + ' s без данни)', 'alarm');
          } else if (d.error && d.error.length > 0) {
            showBanner('⚠️ ' + d.error, 'err');
          } else {
            showBanner('', '');
          }

          el('status').innerText = d.online ? 'Онлайн (преди ' + d.timeSince + ' сек)' : 'Чакане на данни...';
          el('status').style.color = d.linkAlarm ? '#f44336' : '#4CAF50';
        }).catch(error => {
          el('status').innerText = 'Връзката е прекъсната';
          el('status').style.color = '#f44336';
        });
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

// ===========================================================================
//  UART - ИНТЕРАКТИВНО МЕНЮ И КОМАНДИ ЗА КОНФИГУРАЦИЯ
// ===========================================================================

void printUARTMenu() {
  Serial.println(F("\n=========================================="));
  Serial.println(F("           ГЛАВНО МЕНЮ (UART)             "));
  Serial.println(F("=========================================="));
  Serial.println(F("[1] ТЕКУЩ СТАТУС (датчици и релета)"));
  Serial.println(F("[2] ПУСНИ ВСИЧКИ РЕЛЕТА (ALL ON)"));
  Serial.println(F("[3] СПРИ ВСИЧКИ РЕЛЕТА (ALL OFF)"));
  Serial.println(F("[4] РЕСТАРТИРАЙ ГЛАВНИЯ МОДУЛ"));
  Serial.println(F("[5] РЕСТАРТИРАЙ ПОДЧИНЕНИЯ МОДУЛ"));
  Serial.println(F("[6] ПОКАЖИ КОНФИГУРАЦИЯТА"));
  Serial.println(F("[7] ИЗМЕРВАНИЯ (цикъл, памет, връзка, бутони)"));
  Serial.println(F("[M] ТОВА МЕНЮ"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("Отделно реле:  R1 ON | R2 OFF | ... | R4 ON"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("ИЗМЕРВАНИЯ:"));
  Serial.println(F("  PING <1-1000>   тест на закъснението (по подразбиране 100)"));
  Serial.println(F("  LOG ON | LOG OFF  CSV ред за всеки периодичен пакет"));
  Serial.println(F("  RESET STATS     нулира броячите в двата модула"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("КОНФИГУРАЦИЯ (важи след SAVE, някои - след RESTART):"));
  Serial.println(F("  SET SSID <име>          Wi-Fi мрежа"));
  Serial.println(F("  SET PASS <парола>       парола за мрежата"));
  Serial.println(F("  SET NAME <име>          mDNS име (http://<име>.local)"));
  Serial.println(F("  SET MAC <xx:xx:..>      MAC на подчинения модул"));
  Serial.println(F("  SET CHANNEL <1-13>      Wi-Fi канал на подчинения модул"));
  Serial.println(F("  SET INTERVAL <500-60000> интервал на телеметрията, ms"));
  Serial.println(F("  SET VCCOFFSET <±V>      корекция на измереното VCC"));
  Serial.println(F("  SET OTAPASS <парола>    парола за безжично обновяване"));
  Serial.println(F("  SET WEBUSER <име>       потребител за уеб достъп"));
  Serial.println(F("  SET WEBPASS <парола>    парола за уеб достъп"));
  Serial.println(F("  SET WEBAUTH <0|1>       включва/изключва уеб автентикацията"));
  Serial.println(F("  SET LINKTIMEOUT <3-300> праг за авария при мълчание, s"));
  Serial.println(F("  SAVE      запис в EEPROM"));
  Serial.println(F("  PUSH      изпраща конфигурацията към подчинения модул"));
  Serial.println(F("  FACTORY   фабрични настройки + рестарт"));
  Serial.println(F("==========================================\n"));
}

void printConfig() {
  char macBuf[18];
  macToStr(cfg.peerMac, macBuf);
  Serial.println(F("\n--- ТЕКУЩА КОНФИГУРАЦИЯ ---"));
  Serial.print(F("SSID              : ")); Serial.println(cfg.ssid);
  Serial.print(F("Парола            : ")); Serial.println(strlen(cfg.pass) ? F("(зададена)") : F("(празна)"));
  Serial.print(F("mDNS име          : ")); Serial.print(cfg.mdnsName); Serial.println(F(".local"));
  Serial.print(F("IP адрес          : ")); Serial.println(WiFi.localIP());
  Serial.print(F("MAC на хъба       : ")); Serial.println(WiFi.macAddress());
  Serial.print(F("MAC на модула     : ")); Serial.println(macBuf);
  Serial.print(F("Канал на хъба     : ")); Serial.println(WiFi.channel());
  Serial.print(F("Канал на модула   : ")); Serial.println(cfg.wifiChannel);
  Serial.print(F("Интервал телем.   : ")); Serial.print(cfg.telemetryInterval); Serial.println(F(" ms"));
  Serial.print(F("Корекция VCC      : ")); Serial.print(cfg.vccOffset, 2); Serial.println(F(" V"));
  Serial.print(F("Парола за OTA     : ")); Serial.println(strlen(cfg.otaPass) ? F("(зададена)") : F("(без защита!)"));
  Serial.print(F("Уеб автентикация  : ")); Serial.println(cfg.webAuthEnabled ? F("ВКЛЮЧЕНА") : F("ИЗКЛЮЧЕНА"));
  Serial.print(F("Уеб потребител    : ")); Serial.println(cfg.webUser);
  Serial.print(F("Праг за авария    : ")); Serial.print(cfg.linkTimeout); Serial.println(F(" s"));
  Serial.print(F("Криптиране ESP-NOW: ")); Serial.println(USE_ESPNOW_ENCRYPTION ? F("ВКЛЮЧЕНО") : F("ИЗКЛЮЧЕНО"));
  Serial.print(F("Пореден № команда : ")); Serial.println(cmdSeq);
  Serial.print(F("Версия фърмуер    : ")); Serial.print(FW_VERSION);
  Serial.print(F(" / модул: ")); Serial.println(lastT.fwVersion);
  Serial.println(F("---------------------------\n"));
}

void printStatus() {
  const struct_message &t = lastT;
  Serial.println(F("\n--- ТЕКУЩ СТАТУС НА СИСТЕМАТА ---"));
  if (!everReceived) {
    Serial.println(F("⚠️ Все още няма получени данни от подчинения модул."));
  }
  Serial.print(F("Температура    : "));
  if (t.dhtValid) { Serial.print(t.temperature); Serial.println(F(" °C")); }
  else              Serial.println(F("няма валидно четене"));
  Serial.print(F("Влажност       : "));
  if (t.dhtValid) { Serial.print(t.humidity); Serial.println(F(" %")); }
  else              Serial.println(F("няма валидно четене"));
  Serial.print(F("HR202          : ")); Serial.println(t.hr202State ? F("СУХО") : F("МОКРО"));
  Serial.print(F("Блокировка     : ")); Serial.println(slaveLocked() ? F("АКТИВНА") : F("няма"));
  Serial.print(F("Релета         : "));
  for (int i = 0; i < 4; i++) {
    Serial.print(F("[R")); Serial.print(i + 1); Serial.print(F(": "));
    Serial.print(currentRelayState[i] ? F("ВКЛ") : F("ИЗКЛ")); Serial.print(F("] "));
  }
  Serial.println();
  Serial.print(F("Успешни пакети : ")); Serial.println(t.successPackets);
  Serial.print(F("Загубени пакети: ")); Serial.println(t.failedPackets);
  Serial.print(F("Грешки DHT11   : ")); Serial.println(t.dhtErrors);
  Serial.print(F("Причина рестарт: ")); Serial.println(everReceived ? t.resetReason : "N/A");
  Serial.print(F("Свободна RAM   : ")); Serial.print(t.freeHeap); Serial.println(F(" B"));
  Serial.print(F("VCC на модула  : ")); Serial.print(t.vcc, 2); Serial.println(F(" V"));
  Serial.print(F("Цикъл (ср/макс): ")); Serial.print(t.loopAvgUs); Serial.print(F(" / "));
  Serial.print(t.loopMaxUs); Serial.println(F(" µs"));
  Serial.print(F("Uptime на модул: ")); Serial.print(t.uptime); Serial.println(F(" s"));
  unsigned long since = everReceived ? (millis() - lastDataTime) / 1000 : 0;
  Serial.print(F("Последен пакет : преди ")); Serial.print(since); Serial.println(F(" s"));
  Serial.print(F("Команди в опашка: ")); Serial.println(qCount);
  Serial.println(F("---------------------------------\n"));
}

void printMeasurements() {
  const struct_message &t = lastT;
  uint32_t hfree, hmax; uint8_t hfrag;
  ESP.getHeapStats(&hfree, &hmax, &hfrag);

  Serial.println(F("\n--- ИЗМЕРВАНИЯ ---"));
  Serial.printf_P(PSTR("Подчинен: цикъл ср./макс. %lu / %lu µs (прозорец %lu ms, %lu итерации)\n"),
                  (unsigned long)t.loopAvgUs, (unsigned long)t.loopMaxUs,
                  (unsigned long)t.windowMs, (unsigned long)t.loopCount);
  Serial.printf_P(PSTR("          макс. период %lu µs, макс. от старта %lu µs\n"),
                  (unsigned long)t.periodMaxUs, (unsigned long)t.loopMaxEverUs);
  Serial.printf_P(PSTR("          памет своб./мин./блок %lu / %lu / %lu B, фрагм. %u %%\n"),
                  (unsigned long)t.freeHeap, (unsigned long)t.minFreeHeap,
                  (unsigned long)t.maxFreeBlock, t.heapFrag);
  Serial.printf_P(PSTR("Главен:   цикъл ср./макс. %lu / %lu µs (прозорец %lu ms, %lu итерации)\n"),
                  (unsigned long)mPubAvgUs, (unsigned long)mPubMaxUs,
                  (unsigned long)mPubWindowMs, (unsigned long)mPubCount);
  Serial.printf_P(PSTR("          макс. период %lu µs, макс. от старта %lu µs\n"),
                  (unsigned long)mPubPeriodMaxUs, (unsigned long)mLoopMaxEverUs);
  Serial.printf_P(PSTR("          памет своб./мин./блок %lu / %lu / %lu B, фрагм. %u %%, RSSI %d dBm\n"),
                  (unsigned long)hfree, (unsigned long)mMinFreeHeap, (unsigned long)hmax, hfrag,
                  (int)WiFi.RSSI());
  if (ackCount > 0) {
    Serial.printf_P(PSTR("ESP-NOW потвърждение: %lu бр., мин./ср./макс. %lu / %lu / %lu µs, неуспешни %lu\n"),
                    (unsigned long)ackCount, (unsigned long)ackMinUs,
                    (unsigned long)(ackSumUs / ackCount), (unsigned long)ackMaxUs,
                    (unsigned long)ackFail);
  } else {
    Serial.printf_P(PSTR("ESP-NOW потвърждение: няма данни, неуспешни %lu\n"), (unsigned long)ackFail);
  }
  uint32_t total = rxPackets + rxLost;
  Serial.printf_P(PSTR("Прием: %lu приети, %lu загубени, %lu повторни, %lu отхвърлени, %u рестарта на модула\n"),
                  (unsigned long)rxPackets, (unsigned long)rxLost, (unsigned long)rxDup,
                  (unsigned long)rxBad, slaveReboots);
  if (total > 0) {
    Serial.print(F("       доставени: ")); Serial.print(100.0 * rxPackets / total, 2); Serial.println(F(" %"));
  }
  Serial.printf_P(PSTR("PING: %u изпратени, %u успешни, %u загубени"), pingSent, pingOk, pingLost);
  if (pingOk > 0) {
    Serial.printf_P(PSTR(", RTT мин./ср./макс. %lu / %lu / %lu µs"),
                    (unsigned long)rttMinUs, (unsigned long)(rttSumUs / pingOk), (unsigned long)rttMaxUs);
  }
  Serial.println();
  Serial.printf_P(PSTR("Бутон D5: %u натискания, %lu фронта, макс. %u; бутон D6: %u, %lu, макс. %u\n"),
                  t.btnPresses[0], (unsigned long)t.btnEdges[0], t.btnMaxEdges[0],
                  t.btnPresses[1], (unsigned long)t.btnEdges[1], t.btnMaxEdges[1]);
  Serial.printf_P(PSTR("Защита: %u задействания, %u отказа; отхвърлени команди: %u\n"),
                  t.safetyTrips, t.safetyRefusals, t.cmdRejected);
  Serial.print(F("A0: сума ")); Serial.print(t.a0Sum); Serial.print(F(", средно "));
  Serial.print(t.a0Sum / 10.0, 1); Serial.print(F(" -> VCC ")); Serial.print(t.vcc, 3); Serial.println(F(" V"));
  Serial.println(F("------------------\n"));
}

// Нулира броячите на главния модул (подчиненият се нулира с CMD_RESET_STATS)
void resetMasterStats() {
  rxPackets = 0; rxLost = 0; rxDup = 0; rxBad = 0; slaveReboots = 0;
  ackCount = 0; ackFail = 0; ackMinUs = 0xFFFFFFFFUL; ackMaxUs = 0; ackSumUs = 0;
  if (!pingRunning) {
    pingSent = 0; pingOk = 0; pingLost = 0; pingTarget = 0;
    rttMinUs = 0xFFFFFFFFUL; rttMaxUs = 0; rttSumUs = 0;
  }
  mLoopMaxEverUs = 0;
  mMinFreeHeap = ESP.getFreeHeap();
  jsonOverflowCount = 0;
}

void pushConfigToSlave() {
  Serial.println(F("📤 Изпращам конфигурацията към подчинения модул..."));
  enqueueCmd(CMD_CONFIG, 0, false);
  Serial.println(F("   (резултатът се вижда по полетата за конфигурация в следващия пакет телеметрия)"));
}

// Разбор на командите. Пази регистъра на аргументите (важно за SSID/парола)!
void handleSerialCommands() {
  if (Serial.available() <= 0) return;

  String raw = Serial.readStringUntil('\n');
  raw.trim();
  if (raw.length() == 0) return;

  String up = raw;
  up.toUpperCase();

  Serial.print(F("\n⌨️ Въведено: "));
  Serial.println(up.startsWith("SET PASS") || up.startsWith("SET WEBPASS") || up.startsWith("SET OTAPASS")
                 ? "SET *** (скрито)" : raw);

  // ---------- Конфигурационни команди ----------
  if (up.startsWith("SET ")) {
    int sp = raw.indexOf(' ', 4);
    String key = (sp < 0 ? raw.substring(4) : raw.substring(4, sp));
    String val = (sp < 0 ? String("")     : raw.substring(sp + 1));
    key.trim(); key.toUpperCase(); val.trim();

    if (val.length() == 0) { Serial.println(F("❌ Липсва стойност. Пример: SET SSID MyWiFi")); return; }

    if (key == "SSID") {
      strncpy(cfg.ssid, val.c_str(), sizeof(cfg.ssid) - 1); cfg.ssid[sizeof(cfg.ssid)-1] = 0;
      Serial.println(F("✅ SSID е зададен. Изпълнете SAVE и след това 4 (рестарт)."));
    }
    else if (key == "PASS") {
      strncpy(cfg.pass, val.c_str(), sizeof(cfg.pass) - 1); cfg.pass[sizeof(cfg.pass)-1] = 0;
      Serial.println(F("✅ Паролата е зададена. Изпълнете SAVE и след това 4 (рестарт)."));
    }
    else if (key == "NAME") {
      strncpy(cfg.mdnsName, val.c_str(), sizeof(cfg.mdnsName) - 1); cfg.mdnsName[sizeof(cfg.mdnsName)-1] = 0;
      Serial.println(F("✅ Локалното име е зададено. SAVE + рестарт."));
    }
    else if (key == "MAC") {
      uint8_t m[6];
      if (parseMac(val, m)) {
        char macBuf[18];
        memcpy(cfg.peerMac, m, 6);
        macToStr(cfg.peerMac, macBuf);
        Serial.print(F("✅ MAC на модула: ")); Serial.println(macBuf);
        Serial.println(F("   SAVE + рестарт, за да се пренапише peer-ът."));
      } else Serial.println(F("❌ Невалиден формат. Пример: SET MAC BC:FF:4D:1D:A5:A6"));
    }
    else if (key == "CHANNEL") {
      int c = val.toInt();
      if (c >= 1 && c <= 13) {
        cfg.wifiChannel = (uint8_t)c;
        Serial.print(F("✅ Канал на модула: ")); Serial.println(c);
        Serial.println(F("⚠️ Каналът трябва да съвпада с канала на рутера! SAVE + PUSH."));
      } else Serial.println(F("❌ Допустим диапазон: 1-13."));
    }
    else if (key == "INTERVAL") {
      long v = val.toInt();
      if (v >= 500 && v <= 60000) {
        cfg.telemetryInterval = (uint16_t)v;
        Serial.print(F("✅ Интервал: ")); Serial.print(v); Serial.println(F(" ms. SAVE + PUSH."));
      } else Serial.println(F("❌ Допустим диапазон: 500-60000 ms."));
    }
    else if (key == "VCCOFFSET") {
      float v = val.toFloat();
      if (v >= -2.0 && v <= 2.0) {
        cfg.vccOffset = v;
        Serial.print(F("✅ Корекция на VCC: ")); Serial.print(v, 2); Serial.println(F(" V. SAVE + PUSH."));
      } else Serial.println(F("❌ Допустим диапазон: -2.00 до +2.00 V."));
    }
    else if (key == "OTAPASS") {
      strncpy(cfg.otaPass, val.c_str(), sizeof(cfg.otaPass) - 1); cfg.otaPass[sizeof(cfg.otaPass)-1] = 0;
      Serial.println(F("✅ Паролата за OTA е зададена. SAVE + рестарт."));
    }
    else if (key == "WEBUSER") {
      strncpy(cfg.webUser, val.c_str(), sizeof(cfg.webUser) - 1); cfg.webUser[sizeof(cfg.webUser)-1] = 0;
      Serial.println(F("✅ Уеб потребителят е зададен. SAVE."));
    }
    else if (key == "WEBPASS") {
      strncpy(cfg.webPass, val.c_str(), sizeof(cfg.webPass) - 1); cfg.webPass[sizeof(cfg.webPass)-1] = 0;
      Serial.println(F("✅ Уеб паролата е зададена. SAVE."));
    }
    else if (key == "WEBAUTH") {
      cfg.webAuthEnabled = (val.toInt() != 0) ? 1 : 0;
      Serial.print(F("✅ Уеб автентикация: "));
      Serial.println(cfg.webAuthEnabled ? F("ВКЛЮЧЕНА") : F("ИЗКЛЮЧЕНА"));
      Serial.println(F("   SAVE."));
    }
    else if (key == "LINKTIMEOUT") {
      long v = val.toInt();
      if (v >= 3 && v <= 300) { cfg.linkTimeout = (uint16_t)v; Serial.println(F("✅ Прагът е зададен. SAVE.")); }
      else Serial.println(F("❌ Допустим диапазон: 3-300 s."));
    }
    else {
      Serial.println(F("❓ Непознат параметър. Натиснете M за списъка."));
    }
    return;
  }

  if (up == "SAVE")    { saveConfig(); return; }
  if (up == "PUSH")    { pushConfigToSlave(); return; }
  if (up == "FACTORY") {
    Serial.println(F("♻️ Възстановявам фабричните настройки..."));
    setDefaults(); saveConfig(); delay(200); ESP.restart(); return;
  }

  // ---------- Измервания ----------
  if (up == "RESET STATS") {
    resetMasterStats();
    enqueueCmd(CMD_RESET_STATS, 0, false);
    Serial.println(F("🧹 Броячите са нулирани (подчиненият модул - със следващата команда)."));
    return;
  }
  if (up == "LOG ON" || up == "LOG OFF") {
    logEnabled = (up == "LOG ON");
    if (logEnabled)
      Serial.println(F("LOG,t_ms,id,vcc,a0_avg,relays_on,loop_avg_us,loop_max_us,period_max_us,"
                       "loop_count,window_ms,free_heap,min_heap,max_block,frag,mac_ok,mac_fail,"
                       "rx,rx_lost,dht_err,temp,hum,m_loop_avg_us,m_loop_max_us,m_free_heap"));
    else
      Serial.println(F("📝 Записът е спрян."));
    return;
  }
  if (up == "PING" || up.startsWith("PING ")) {
    long n = (up.length() > 5) ? up.substring(5).toInt() : 100;
    if (!startPingTest(n))
      Serial.println(pingRunning ? F("❌ Тестът вече се изпълнява.") : F("❌ Допустим брой: 1-1000."));
    return;
  }

  // ---------- Оперативни команди ----------
  if (up == "M" || up == "MENU" || up == "HELP") { printUARTMenu();     return; }
  if (up == "6" || up == "CONFIG")               { printConfig();       return; }
  if (up == "1" || up == "STATUS")               { printStatus();       return; }
  if (up == "7" || up == "STATS")                { printMeasurements(); return; }

  if (up == "2" || up == "ALL ON") {
    if (slaveLocked()) { Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (!currentRelayState[i] && enqueueRelay(i, true)) n++;
    Serial.print(F("📥 Поставени в опашката: ")); Serial.print(n); Serial.println(F(" команди."));
    return;
  }
  if (up == "3" || up == "ALL OFF") {
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (enqueueRelay(i, false)) n++;
    Serial.print(F("📥 Поставени в опашката: ")); Serial.print(n); Serial.println(F(" команди."));
    return;
  }
  if (up == "4" || up == "RESTART") {
    Serial.println(F("🔄 Рестартиране на главния модул..."));
    saveConfig(); delay(150); ESP.restart(); return;
  }
  if (up == "5" || up == "SLAVE RESTART") {
    Serial.println(F("🔄 Изпращам команда за рестарт към подчинения модул..."));
    enqueueCmd(CMD_RESTART, 0, false);
    return;
  }

  // R<n> ON / OFF
  if (up.startsWith("R") && up.length() >= 5) {
    int relayNum = up.substring(1, 2).toInt() - 1;
    String action = up.substring(3); action.trim();
    if (relayNum >= 0 && relayNum < 4 && (action == "ON" || action == "OFF")) {
      bool targetState = (action == "ON");
      if (targetState && slaveLocked()) { Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
      if (enqueueRelay(relayNum, targetState)) {
        Serial.print(F("📥 Реле ")); Serial.print(relayNum + 1);
        Serial.println(targetState ? F(" -> ВКЛ (в опашката)") : F(" -> ИЗКЛ (в опашката)"));
      }
      return;
    }
    Serial.println(F("❌ Формат: R1 ON, R2 OFF ... R4 ON"));
    return;
  }

  Serial.println(F("❓ Непозната команда. Натиснете 'M' за менюто."));
}

// CSV ред за всеки периодичен пакет (LOG ON) - за графиките в експерименталната част
void printLogLine() {
  if (!logPending) return;
  logPending = false;
  if (!logEnabled) return;
  const struct_message &t = lastT;
  uint8_t on = 0;
  for (int i = 0; i < 4; i++) if (t.relayState[i]) on++;
  Serial.printf_P(PSTR("LOG,%lu,%lu,%.3f,%.1f,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%lu,%lu,%lu,%lu,%u,%.1f,%.1f,%lu,%lu,%lu\n"),
                  (unsigned long)millis(), (unsigned long)t.readingId, (double)t.vcc,
                  (double)(t.a0Sum / 10.0f), on,
                  (unsigned long)t.loopAvgUs, (unsigned long)t.loopMaxUs, (unsigned long)t.periodMaxUs,
                  (unsigned long)t.loopCount, (unsigned long)t.windowMs,
                  (unsigned long)t.freeHeap, (unsigned long)t.minFreeHeap, (unsigned long)t.maxFreeBlock,
                  t.heapFrag, (unsigned long)t.successPackets, (unsigned long)t.failedPackets,
                  (unsigned long)rxPackets, (unsigned long)rxLost, t.dhtErrors,
                  (double)t.temperature, (double)t.humidity,
                  (unsigned long)mPubAvgUs, (unsigned long)mPubMaxUs, (unsigned long)ESP.getFreeHeap());
}

// ===========================================================================
//  ESP-NOW CALLBACKS
//  Вътре в callback не се пише по UART и не се заделя динамична памет.
// ===========================================================================

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  (void)mac_addr;
  uint32_t d = micros() - txStartUs;             // време до потвърждението (MAC ниво)
  if (sendStatus == 0) {
    ackCount++;
    ackSumUs += d;
    if (d < ackMinUs) ackMinUs = d;
    if (d > ackMaxUs) ackMaxUs = d;
  } else {
    ackFail++;
  }
  deliverySuccess = (sendStatus == 0);
  waitingForDelivery = false;
}

// Пакетът е от подчинения модул. Той работи в режим AP+STA и може да изпраща
// и от MAC адреса на точката за достъп, който при ESP8266 се различава само
// по бит 1 на първия байт - затова този бит не се сравнява.
bool fromSlave(const uint8_t *mac) {
  return ((mac[0] | 0x02) == (cfg.peerMac[0] | 0x02)) && memcmp(mac + 1, cfg.peerMac + 1, 5) == 0;
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  uint32_t rxUs = micros();
  if (len >= 1 && incomingData[0] != PROTO_VERSION) {            // друга версия
    rxBad++;
    if (fromSlave(mac)) { rxBadProto = incomingData[0]; rxBadTime = millis(); }
    return;
  }
  if (len != sizeof(incomingTelemetry)) { rxBad++; return; }      // защита от чужд пакет
  memcpy(&incomingTelemetry, incomingData, sizeof(incomingTelemetry));
  struct_message &t = incomingTelemetry;
  t.resetReason[sizeof(t.resetReason) - 1] = '\0';

  // Загубени пакети - по пропуските в поредните номера на подчинения модул
  if (haveReadingId) {
    if (t.readingId == lastReadingId) { rxDup++; return; }
    bool restarted = (t.readingId < lastReadingId) || (t.uptime < lastT.uptime);
    if (restarted) slaveReboots++;                 // номерацията е започнала отначало
    else           rxLost += t.readingId - lastReadingId - 1;
  }
  lastReadingId = t.readingId;
  haveReadingId = true;
  rxPackets++;

  memcpy(&lastT, &t, sizeof(lastT));
  for (int i = 0; i < 4; i++) currentRelayState[i] = t.relayState[i];
  lastDataTime = millis();
  everReceived = true;
  if (t.reportType == REPORT_PERIODIC) logPending = true;

  // Край на измерването на RTT: отчет с номера на изпратената заявка
  if (pingOutstanding && t.ackSeq == pingSeq) {
    uint32_t rtt = rxUs - pingT0Us;
    pingOutstanding = false;
    pingOk++;
    rttSumUs += rtt;
    if (rtt < rttMinUs) rttMinUs = rtt;
    if (rtt > rttMaxUs) rttMaxUs = rtt;
    pingLastRttUs = rtt;
    pingResultPending = true;
  }
}

// ===========================================================================
//  ЗАЩИТА НА УЕБ ДОСТЪПА
// ===========================================================================

bool ensureAuth() {
  if (!cfg.webAuthEnabled) return true;
  if (server.authenticate(cfg.webUser, cfg.webPass)) return true;
  server.requestAuthentication();
  return false;
}

// ===========================================================================
//  ОБРАБОТЧИЦИ НА ЗАЯВКИТЕ (REST API)
// ===========================================================================

void handleConfigRequest() {
  if (!ensureAuth()) return;
  char ipBuf[16], macBuf[18];
  IPAddress ip = WiFi.localIP();
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  macToStr(cfg.peerMac, macBuf);

  jsonBegin();
  jsonStr  ("ssid",         cfg.ssid);
  jsonStr  ("name",         cfg.mdnsName);
  jsonStr  ("ip",           ipBuf);
  jsonStr  ("mac",          macBuf);
  jsonU32  ("hubChannel",   WiFi.channel());
  jsonU32  ("slaveChannel", cfg.wifiChannel);
  jsonU32  ("interval",     cfg.telemetryInterval);
  jsonFloat("vccOffset",    cfg.vccOffset, 2);
  jsonU32  ("fwMaster",     FW_VERSION);
  jsonEnd();
  sendJson(200);
}

void handleDataRequest() {
  if (!ensureAuth()) return;
  const struct_message &t = lastT;
  unsigned long since = everReceived ? (millis() - lastDataTime) / 1000 : 0;
  bool linkAlarm = (!everReceived) || (since > cfg.linkTimeout);
  const char *err = (lastErrorMsg[0] && millis() - lastErrorTime < 8000) ? lastErrorMsg : "";
  uint8_t protoWarn = (rxBadProto && millis() - rxBadTime < 10000) ? rxBadProto : 0;
  uint32_t hfree, hmax; uint8_t hfrag;
  ESP.getHeapStats(&hfree, &hmax, &hfrag);

  jsonBegin();
  jsonBool ("online",      everReceived);
  jsonU32  ("timeSince",   since);
  jsonBool ("linkAlarm",   linkAlarm);
  jsonU32  ("id",          t.readingId);
  jsonFloat("temperature", t.temperature, 2, everReceived && t.dhtValid);
  jsonFloat("humidity",    t.humidity,    2, everReceived && t.dhtValid);
  jsonBool ("hr202",       t.hr202State);
  jsonBool ("locked",      slaveLocked());
  jsonKey  ("relays");
  jsonPrintf("[%s,%s,%s,%s]",
             currentRelayState[0] ? "true" : "false", currentRelayState[1] ? "true" : "false",
             currentRelayState[2] ? "true" : "false", currentRelayState[3] ? "true" : "false");
  jsonFloat("vcc",         t.vcc, 2, everReceived);
  jsonFloat("a0",          t.a0Sum / 10.0f, 1, everReceived);
  jsonU32  ("freeHeap",    t.freeHeap);
  jsonU32  ("uptime",      t.uptime);
  jsonStr  ("resetReason", everReceived ? t.resetReason : "N/A");
  jsonU32  ("dhtErrors",   t.dhtErrors);
  jsonU32  ("success",     t.successPackets);
  jsonU32  ("failed",      t.failedPackets);
  jsonU32  ("queue",       qCount);
  jsonStr  ("error",       err);
  jsonU32  ("protoWarn",   protoWarn);
  jsonU32  ("proto",       PROTO_VERSION);
  jsonU32  ("fwMaster",    FW_VERSION);
  jsonU32  ("fwSlave",     t.fwVersion);

  jsonObjBegin("s");                         // измервания в подчинения модул
  jsonU32("loopAvg",     t.loopAvgUs);
  jsonU32("loopMax",     t.loopMaxUs);
  jsonU32("loopMaxEver", t.loopMaxEverUs);
  jsonU32("periodMax",   t.periodMaxUs);
  jsonU32("loopCount",   t.loopCount);
  jsonU32("window",      t.windowMs);
  jsonU32("minHeap",     t.minFreeHeap);
  jsonU32("maxBlock",    t.maxFreeBlock);
  jsonU32("frag",        t.heapFrag);
  jsonU32("b1p",         t.btnPresses[0]);
  jsonU32("b1e",         t.btnEdges[0]);
  jsonU32("b1m",         t.btnMaxEdges[0]);
  jsonU32("b2p",         t.btnPresses[1]);
  jsonU32("b2e",         t.btnEdges[1]);
  jsonU32("b2m",         t.btnMaxEdges[1]);
  jsonU32("rejected",    t.cmdRejected);
  jsonU32("trips",       t.safetyTrips);
  jsonU32("refusals",    t.safetyRefusals);
  jsonObjEnd();

  jsonObjBegin("m");                         // измервания в главния модул
  jsonU32("loopAvg",     mPubAvgUs);
  jsonU32("loopMax",     mPubMaxUs);
  jsonU32("loopMaxEver", mLoopMaxEverUs);
  jsonU32("periodMax",   mPubPeriodMaxUs);
  jsonU32("loopCount",   mPubCount);
  jsonU32("window",      mPubWindowMs);
  jsonU32("heap",        hfree);
  jsonU32("minHeap",     mMinFreeHeap);
  jsonU32("frag",        hfrag);
  jsonU32("uptime",      millis() / 1000);
  jsonI32("rssi",        WiFi.RSSI());
  jsonU32("jsonOverflows", jsonOverflowCount);
  jsonObjEnd();

  jsonObjBegin("link");                      // връзка ESP-NOW
  jsonU32("rx",      rxPackets);
  jsonU32("lost",    rxLost);
  jsonU32("dup",     rxDup);
  jsonU32("bad",     rxBad);
  jsonU32("reboots", slaveReboots);
  jsonU32("ackN",    ackCount);
  jsonU32("ackFail", ackFail);
  if (ackCount > 0) {
    jsonU32("ackMin", ackMinUs);
    jsonU32("ackAvg", (uint32_t)(ackSumUs / ackCount));
    jsonU32("ackMax", ackMaxUs);
  } else {
    jsonNull("ackMin"); jsonNull("ackAvg"); jsonNull("ackMax");
  }
  jsonObjEnd();

  jsonObjBegin("ping");                      // тест на закъснението
  jsonBool("run",    pingRunning);
  jsonU32 ("target", pingTarget);
  jsonU32 ("sent",   pingSent);
  jsonU32 ("ok",     pingOk);
  jsonU32 ("lost",   pingLost);
  if (pingOk > 0) {
    jsonU32("min", rttMinUs);
    jsonU32("avg", (uint32_t)(rttSumUs / pingOk));
    jsonU32("max", rttMaxUs);
  } else {
    jsonNull("min"); jsonNull("avg"); jsonNull("max");
  }
  jsonObjEnd();

  jsonEnd();
  sendJson(200);
}

// ===========================================================================
//  SETUP
// ===========================================================================

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);           // не блокира loop() при непълен ред
  Serial.println(F("\n\n--- СТАРТИРАНЕ НА ГЛАВНИЯ МОДУЛ ---"));

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  // До първия пакет се приема, че е сухо и няма блокировка
  memset(&lastT, 0, sizeof(lastT));
  lastT.hr202State = true;

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(cfg.mdnsName);
  WiFi.begin(cfg.ssid, cfg.pass);

  Serial.print(F("Свързване към мрежа: ")); Serial.println(cfg.ssid);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500); Serial.print('.'); attempts++;
    // позволява конфигуриране по UART дори когато мрежата е недостъпна
    handleSerialCommands();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n❌ Няма връзка с мрежата."));
    Serial.println(F("   Задайте SET SSID / SET PASS, после SAVE и 4 (рестарт)."));
    printUARTMenu();
    // Не рестартира веднага - иначе конфигурирането по UART е невъзможно.
    unsigned long t0 = millis();
    while (millis() - t0 < 120000) { handleSerialCommands(); delay(10); }
    ESP.restart();
  }

  // Случаен начален номер: след рестарт на главния модул първата команда
  // не може да съвпадне с последния номер, запомнен от подчинения модул.
  cmdSeq = ESP.random();

  Serial.println(F("\n✅ Свързан успешно!"));
  Serial.print(F(">>> IP адрес: ")); Serial.println(WiFi.localIP());
  Serial.print(F(">>> Канал   : ")); Serial.println(WiFi.channel());

  if (WiFi.channel() != cfg.wifiChannel) {
    Serial.println(F("\n⚠️ ВНИМАНИЕ: каналът на рутера се различава от този на подчинения модул!"));
    Serial.print(F("   Рутер: ")); Serial.print(WiFi.channel());
    Serial.print(F("   |   Модул: ")); Serial.println(cfg.wifiChannel);
    Serial.println(F("   ESP-NOW няма да работи. Изпълнете SET CHANNEL <канала на рутера>,"));
    Serial.println(F("   после SAVE и PUSH, или фиксирайте канала на рутера.\n"));
  }

  MDNS.begin(cfg.mdnsName);
  MDNS.addService("http", "tcp", 80);
  LLMNR.begin(cfg.mdnsName);

  if (esp_now_init() != 0) { Serial.println(F("❌ Грешка при инициализация на ESP-NOW!")); return; }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

#if USE_ESPNOW_ENCRYPTION
  esp_now_set_kok(kokKey, 16);
  esp_now_add_peer(cfg.peerMac, ESP_NOW_ROLE_COMBO, WiFi.channel(), lmkKey, 16);
  Serial.println(F("🔒 ESP-NOW работи с криптиране."));
#else
  esp_now_add_peer(cfg.peerMac, ESP_NOW_ROLE_COMBO, WiFi.channel(), NULL, 0);
  Serial.println(F("🔓 ESP-NOW работи без криптиране (виж USE_ESPNOW_ENCRYPTION)."));
#endif

  // ------------------- УЕБ СЪРВЪР -------------------
  server.on("/", []() {
    if (!ensureAuth()) return;
    server.send_P(200, "text/html", index_html);
  });

  server.on("/toggle", []() {
    if (!ensureAuth()) return;
    int idx = server.hasArg("id") ? server.arg("id").toInt() : -1;
    if (idx < 0 || idx > 3) { sendJsonError(400, "Невалиден индекс"); return; }
    bool intended = !currentRelayState[idx];
    if (intended && slaveLocked()) { sendJsonError(403, "Отчетена е влага - включването е блокирано"); return; }
    if (!enqueueRelay(idx, intended)) { sendJsonError(503, "Опашката е препълнена"); return; }
    // Оптимистичен отговор; реалното състояние идва с отчета на подчинения модул.
    jsonBegin(); jsonBool("state", intended); jsonEnd();
    sendJson(200);
  });

  server.on("/onAll", []() {
    if (!ensureAuth()) return;
    if (slaveLocked()) { sendJsonError(403, "Отчетена е влага"); return; }
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (!currentRelayState[i] && enqueueRelay(i, true)) n++;
    jsonBegin(); jsonU32("queued", n); jsonEnd();
    sendJson(200);
  });

  server.on("/offAll", []() {
    if (!ensureAuth()) return;
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (enqueueRelay(i, false)) n++;
    jsonBegin(); jsonU32("queued", n); jsonEnd();
    sendJson(200);
  });

  server.on("/ping", []() {
    if (!ensureAuth()) return;
    long n = server.hasArg("n") ? server.arg("n").toInt() : 100;
    if (!startPingTest(n)) {
      sendJsonError(409, pingRunning ? "Тестът вече се изпълнява" : "Допустим брой: 1-1000");
      return;
    }
    jsonBegin(); jsonBool("started", true); jsonU32("n", (uint32_t)n); jsonEnd();
    sendJson(200);
  });

  server.on("/resetStats", []() {
    if (!ensureAuth()) return;
    resetMasterStats();
    enqueueCmd(CMD_RESET_STATS, 0, false);
    jsonBegin(); jsonBool("ok", true); jsonEnd();
    sendJson(200);
  });

  server.on("/config", handleConfigRequest);
  server.on("/data",   handleDataRequest);

  server.begin();

  // ------------------- OTA -------------------
  ArduinoOTA.setHostname("ESP8266-Hub");
  if (strlen(cfg.otaPass) > 0) {
    ArduinoOTA.setPassword(cfg.otaPass);
    Serial.println(F("🔒 Безжичното обновяване е защитено с парола."));
  } else {
    Serial.println(F("⚠️ OTA е БЕЗ парола! Задайте SET OTAPASS <парола>."));
  }
  ArduinoOTA.begin();

  printUARTMenu();

  mMinFreeHeap = ESP.getFreeHeap();
  mWindowStartMs = millis();
}

// ===========================================================================
//  LOOP
// ===========================================================================

void loop() {
  uint32_t t0 = micros();
  if (mHavePrev) {                                    // период между две итерации
    uint32_t period = t0 - mPrevStartUs;
    if (period > mPeriodMaxUs) mPeriodMaxUs = period;
  }
  mPrevStartUs = t0;
  mHavePrev = true;

  server.handleClient();
  ArduinoOTA.handle();
  MDNS.update();
  handleSerialCommands();
  processQueue();          // неблокиращо изпълнение на командите
  processPingTest();       // тест на закъснението (ако е пуснат)
  printLogLine();          // CSV запис на телеметрията (LOG ON)

  uint32_t fh = ESP.getFreeHeap();
  if (fh < mMinFreeHeap) mMinFreeHeap = fh;

  uint32_t dt = micros() - t0;                        // време за изпълнение
  mCount++;
  mSumUs += dt;
  if (dt > mMaxUs)          mMaxUs = dt;
  if (dt > mLoopMaxEverUs)  mLoopMaxEverUs = dt;

  uint32_t nowMs = millis();                          // затваряне на прозореца
  if (nowMs - mWindowStartMs >= MSTATS_WINDOW_MS) {
    mPubCount       = mCount;
    mPubAvgUs       = mCount ? (uint32_t)(mSumUs / mCount) : 0;
    mPubMaxUs       = mMaxUs;
    mPubPeriodMaxUs = mPeriodMaxUs;
    mPubWindowMs    = nowMs - mWindowStartMs;
    mCount = 0; mSumUs = 0; mMaxUs = 0; mPeriodMaxUs = 0;
    mWindowStartMs  = nowMs;
  }
}

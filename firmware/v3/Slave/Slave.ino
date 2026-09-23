/*
 * ============================================================================
 *  СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС
 *  ПОДЧИНЕН МОДУЛ (SLAVE)
 *  ----------------------------------------------------------------------------
 *  Дипломна работа - ТУ София, ФЕТТ, катедра "Електронна техника"
 *  Дипломант: инж. Кръстиян Тодоров Праматаров, ф. № 901322003
 *  Ръководител: доц. д-р инж. Любомир Богданов
 *  ----------------------------------------------------------------------------
 *  Версия 3.0 - промени спрямо 2.1:
 *    - Реле 4 е преместено от D3 (GPIO0) на D8 (GPIO15) - GPIO0 определя
 *      режима на зареждане и не бива да се товари от входа на релейния модул
 *    - Времето за цикъл се измерва с micros(): средно и максимално време за
 *      изпълнение на loop() за всеки пакет, максимален период между две
 *      итерации, брой итерации и максимум от стартирането
 *    - Измерването на VCC (10 проби на A0) не блокира loop() - по една проба
 *      на итерация; една обща функция за VCC, в която се прилага и VCCOFFSET
 *      (премахнато е невалидното ESP.getVcc() от статуса)
 *    - Незабавен отчет след всяка команда и всяко локално събитие, с номера на
 *      изпълнената команда - за бързо обновяване и за измерване на закъснението
 *    - Броене на фронтовете на бутоните (по прекъсване) - за оценка на трептенето
 *    - Защита от влага със задръжка 2 s при изсъхване (срещу трептене на изхода
 *      на компаратора около прага)
 *    - Минимална свободна памет и фрагментация на динамичната памет
 *    - Команди се приемат само от главния модул (по MAC); дубликатите се
 *      отхвърлят правилно и след рестарт на главния модул
 * ============================================================================
 */

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <DHT.h>
#include <EEPROM.h>


extern "C" {
  #include <user_interface.h>
}

// ---------------------------------------------------------------------------
//  КРИПТИРАНЕ НА ESP-NOW - трябва да съвпада с настройката в MASTER.INO!
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

// ---------------------------------------------------------------------------
//  ПИНОВЕ
//  Реле 4 е на D8 (GPIO15). GPIO15 трябва да е на ниско ниво при стартиране -
//  NodeMCU го държи там с pull-down резистор, а входът на релейния модул в
//  режим "H" не го изтегля нагоре. Така платката зарежда нормално и релето
//  остава изключено по време на зареждането.
//  ВНИМАНИЕ: джъмперът на модула на реле 4 трябва да е в положение "H".
//  В положение "L" входът тегли към +5 V и платката няма да стартира.
//  D3 (GPIO0) и D4 (GPIO2) трябва да са на високо ниво при стартиране -
//  не се свързват към товари, които ги теглят към маса.
//  GPIO3 (RX) е свободен - серийният порт се използва за UART менюто.
// ---------------------------------------------------------------------------
const uint8_t relayPins[4] = {D0, D1, D2, D8};  // изходи за четирите релета
#define DHTPIN            D4    // датчик за температура и влажност
#define BUTTON_TOGGLE_PIN D5    // бутон 1: превключва реле 1
#define BUTTON_OFF_PIN    D6    // бутон 2: аварийно изключване
#define HR202_PIN         D7    // датчик за наличие на влага (LOW = влага)
#define DHTTYPE           DHT11

DHT dht(DHTPIN, DHTTYPE);

// ---------------------------------------------------------------------------
//  ИЗМЕРВАНЕ НА НАПРЕЖЕНИЕТО (A0)
//  VCC = (средно A0 / 1024) * VCC_FULL_SCALE_V - n * VCC_RELAY_ERR_V + VCCOFFSET
//  n - брой включени релета. Двата коефициента са калибрирани с мултиметър.
// ---------------------------------------------------------------------------
#define VCC_FULL_SCALE_V   10.91f   // V, при които A0 би дал 1024 (делител + калибровка)
#define VCC_RELAY_ERR_V    0.055f   // V, системна грешка при едно включено реле
#define VCC_SAMPLES        10       // брой проби за осредняване
#define VCC_SAMPLE_GAP_US  2000UL   // най-малко време между две проби, µs

#define DEBOUNCE_MS          60     // филтър срещу трептене на бутоните
#define SAFETY_RELEASE_MS    2000   // колко дълго трябва да е сухо, за да се свали блокировката
#define DHT_MIN_INTERVAL_MS  2000   // DHT11 не се чете по-често от това
#define REPORT_MIN_GAP_MS    50     // най-малък интервал между отчетите за локални събития

// ===========================================================================
//  СТРУКТУРИ НА ОБМЕНА  (ЗАДЪЛЖИТЕЛНО ИДЕНТИЧНИ С MASTER.INO!)
// ===========================================================================

// Вид на отчета (телеметрията)
#define REPORT_PERIODIC 0   // периодичен пакет (на всеки cfgInterval)
#define REPORT_COMMAND  1   // отговор след изпълнена команда
#define REPORT_EVENT    2   // локално събитие: бутон, влага, UART

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

struct_message outgoingTelemetry;
struct_control incomingControl;

// ===========================================================================
//  КОНФИГУРАЦИЯ В EEPROM
// ===========================================================================

#define CFG_MAGIC   0x484D5331UL   // "HMS1"
#define CFG_VERSION 3              // 3 - VCCOFFSET вече се прилага към VCC
#define EEPROM_SIZE 256

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  peerMac[6];        // MAC на главния модул
    uint8_t  wifiChannel;
    uint16_t telemetryInterval;
    float    vccOffset;
    uint32_t crc;
} SlaveConfig;

SlaveConfig cfg;

// --------------------------- СЪСТОЯНИЕ -------------------------------------
uint32_t readingCounter = 0;
bool     currentRelayState[4] = {false, false, false, false};
bool     isSafetyLocked = false;
unsigned long lastWetMs = 0;
uint16_t safetyTrips = 0, safetyRefusals = 0;

// Входяща команда: записва се в callback-а, изпълнява се в loop()
volatile bool     commandPending      = false;
volatile uint8_t  pendingCmdType      = CMD_RELAY;
volatile uint8_t  pendingRelayIndex   = 0;
volatile bool     pendingRelayCommand = false;
volatile uint16_t pendingInterval     = 2000;
volatile float    pendingVccOffset    = 0.0;
volatile uint8_t  pendingChannel      = 6;
volatile uint32_t pendingSeq          = 0;
volatile uint16_t cmdRejected         = 0;
volatile bool     foreignMacSeen      = false;  // за съобщение по UART от loop()
volatile uint8_t  badProtoSeen        = 0;      // версия на отхвърлен пакет
uint32_t lastSeq = 0;
bool     haveLastSeq = false;                   // lastSeq е валиден
uint32_t ackSeq = 0;                            // последната изпълнена команда
unsigned long lastLinkWarnMs = 0;
bool     linkWarnShown = false;                 // вече е изведено поне едно съобщение

char     systemResetReason[32] = "";
volatile uint32_t successPackets = 0, failedPackets = 0;
uint16_t dhtErrors = 0;
float    lastValidTemp = 0.0, lastValidHum = 0.0;
bool     haveValidReading = false;
unsigned long lastDhtRead = 0;
bool     dhtEverRead = false;

unsigned long lastTelemetrySend = 0;
bool     reportPending = false;
uint8_t  reportPendingType = REPORT_EVENT;
unsigned long lastReportMs = 0;

// ---------------------------------------------------------------------------
//  ВРЕМЕ ЗА ЦИКЪЛ (micros(), разделителна способност 1 µs)
//  Изпълнение - от началото до края на loop(), т.е. работата на програмата.
//  Период     - между началата на две поредни итерации; включва и времето,
//               в което системата обслужва Wi-Fi стека между итерациите.
//  Стойностите се натрупват в "прозорец" между два периодични пакета.
// ---------------------------------------------------------------------------
uint32_t lsCount = 0, lsMaxUs = 0, lsPeriodMaxUs = 0;
uint64_t lsSumUs = 0;
uint32_t lsWindowStartMs = 0;
uint32_t lsPrevStartUs = 0;
bool     lsHavePrev = false;
uint32_t loopMaxEverUs = 0;
// последният завършен прозорец (изпраща се в отчетите)
uint32_t pubLoopCount = 0, pubLoopAvgUs = 0, pubLoopMaxUs = 0;
uint32_t pubPeriodMaxUs = 0, pubWindowMs = 0;

uint32_t minFreeHeap = 0xFFFFFFFFUL;

// ---------------------------------------------------------------------------
//  Неблокиращо измерване на VCC: 10 проби, по една на итерация, през >= 2 ms
// ---------------------------------------------------------------------------
bool     vccBusy = false;
uint8_t  vccIdx = 0;
uint16_t vccSum = 0;
uint32_t vccLastUs = 0;
uint16_t lastA0Sum = 0;
float    lastVcc = 0.0;

// Неблокиращо отстраняване на трептенето (заменя delay() в цикъла)
bool toggleStable = HIGH, toggleLastRead = HIGH;
bool offStable = HIGH,    offLastRead = HIGH;
unsigned long toggleLastChange = 0, offLastChange = 0;

// Броячи за бутоните (индекс 0 - D5, индекс 1 - D6)
volatile uint32_t btnEdges[2] = {0, 0};     // от прекъсването - всички фронтове
uint32_t btnEdgesAtLast[2] = {0, 0};        // стойност при последното прието превключване
uint16_t btnPresses[2] = {0, 0};
uint8_t  btnMaxEdges[2] = {0, 0};

// ===========================================================================
//  EEPROM
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
  uint8_t defMac[6] = {0xA4, 0xE5, 0x7C, 0x01, 0xE6, 0x9E};   // MAC на главния модул
  memcpy(cfg.peerMac, defMac, 6);
  cfg.wifiChannel       = 6;
  cfg.telemetryInterval = 2000;
  cfg.vccOffset         = 0.0;
}

void saveConfig() {
  cfg.crc = crc32((uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  EEPROM.put(0, cfg);
  if (EEPROM.commit()) Serial.println(F("💾 Конфигурацията е записана в EEPROM."));
  else                 Serial.println(F("❌ Грешка при запис в EEPROM!"));
}

void loadConfig() {
  EEPROM.get(0, cfg);
  uint32_t calc = crc32((uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  bool intact = (cfg.magic == CFG_MAGIC && cfg.crc == calc);

  if (intact && cfg.version == CFG_VERSION) {
    Serial.println(F("✅ Конфигурацията е прочетена от EEPROM."));
  } else if (intact && cfg.version == 2) {
    // Разположението на полетата е същото като във версия 2. Там VCCOFFSET не
    // се прилагаше към изпращаното напрежение (ефективно е бил 0 V), затова се
    // записва 0 - калибрираното показание остава непроменено.
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

bool isBroadcastMac(const uint8_t *m) {
  for (int i = 0; i < 6; i++) if (m[i] != 0xFF) return false;
  return true;
}

// ===========================================================================
//  УПРАВЛЕНИЕ НА РЕЛЕТАТА (обща точка за бутони, UART и ESP-NOW)
// ===========================================================================

bool applyRelay(uint8_t idx, bool state) {
  if (idx > 3) return false;
  if (isSafetyLocked && state) { safetyRefusals++; return false; }  // включване при влага е забранено
  currentRelayState[idx] = state;
  digitalWrite(relayPins[idx], state ? HIGH : LOW);
  return true;
}

void allRelaysOff() {
  for (int i = 0; i < 4; i++) {
    currentRelayState[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
}

uint8_t countActiveRelays() {
  uint8_t n = 0;
  for (int i = 0; i < 4; i++) if (currentRelayState[i]) n++;
  return n;
}

// Заявка за незабавен отчет към главния модул (изпраща се от handleTelemetry)
void requestReport(uint8_t type) {
  if (!reportPending || type == REPORT_COMMAND) reportPendingType = type;
  reportPending = true;
}

// ===========================================================================
//  ПРЕКЪСВАНИЯ ОТ БУТОНИТЕ - само броят фронтовете (за измерване на трептенето).
//  Логиката на бутоните остава в loop() с програмния филтър.
// ===========================================================================

void IRAM_ATTR isrButtonToggle() { btnEdges[0]++; }
void IRAM_ATTR isrButtonOff()    { btnEdges[1]++; }

// ===========================================================================
//  ESP-NOW CALLBACKS
//  Вътре в callback НЕ се пишат Serial.print и НЕ се прави тежка обработка.
// ===========================================================================

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  (void)mac_addr;
  if (sendStatus == 0) successPackets++;
  else                 failedPackets++;
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (!isBroadcastMac(cfg.peerMac) && memcmp(mac, cfg.peerMac, 6) != 0) {
    foreignMacSeen = true; cmdRejected++; return;                 // не е от главния модул
  }
  if (len >= 1 && incomingData[0] != PROTO_VERSION) {           // главен модул с друга версия
    badProtoSeen = incomingData[0]; cmdRejected++; return;
  }
  if (len != sizeof(incomingControl)) { cmdRejected++; return; }
  memcpy(&incomingControl, incomingData, sizeof(incomingControl));

  if (haveLastSeq && incomingControl.seq == lastSeq) {           // дублиран пакет
    cmdRejected++; return;
  }
  lastSeq = incomingControl.seq;
  haveLastSeq = true;

  if (commandPending) cmdRejected++;       // предишната команда не е изпълнена - презаписва се
  pendingCmdType      = incomingControl.cmdType;
  pendingRelayIndex   = incomingControl.relayIndex;
  pendingRelayCommand = incomingControl.relayState;
  pendingInterval     = incomingControl.telemetryInterval;
  pendingVccOffset    = incomingControl.vccOffset;
  pendingChannel      = incomingControl.wifiChannel;
  pendingSeq          = incomingControl.seq;
  commandPending      = true;
}

// ===========================================================================
//  ИЗМЕРВАНЕ НА НАПРЕЖЕНИЕТО - една обща функция за телеметрията и за UART
// ===========================================================================

float computeVcc(uint16_t a0Sum, uint8_t activeRelays) {
  float a0Avg = a0Sum / (float)VCC_SAMPLES;
  float v = (a0Avg / 1024.0f) * VCC_FULL_SCALE_V;   // базова сметка по делителя
  v -= activeRelays * VCC_RELAY_ERR_V;              // корекция за включените релета
  return v + cfg.vccOffset;                         // корекция, зададена от потребителя
}

void startVccBurst() {
  vccBusy = true;
  vccIdx = 0;
  vccSum = 0;
  vccLastUs = micros() - VCC_SAMPLE_GAP_US;         // първата проба - веднага
}

// Взема най-много една проба. Връща true, когато серията току-що е завършила.
bool serviceVccBurst() {
  if (!vccBusy) return false;
  uint32_t now = micros();
  if (now - vccLastUs < VCC_SAMPLE_GAP_US) return false;
  vccLastUs = now;
  vccSum += analogRead(A0);
  if (++vccIdx < VCC_SAMPLES) return false;
  vccBusy = false;
  lastA0Sum = vccSum;
  lastVcc = computeVcc(lastA0Sum, countActiveRelays());
  return true;
}

// ===========================================================================
//  СТАТИСТИКА НА ЦИКЪЛА
// ===========================================================================

// Затваря текущия прозорец и запомня стойностите му за изпращане
void publishLoopWindow() {
  uint32_t nowMs = millis();
  pubLoopCount   = lsCount;
  pubLoopAvgUs   = lsCount ? (uint32_t)(lsSumUs / lsCount) : 0;
  pubLoopMaxUs   = lsMaxUs;
  pubPeriodMaxUs = lsPeriodMaxUs;
  pubWindowMs    = nowMs - lsWindowStartMs;
  lsCount = 0; lsSumUs = 0; lsMaxUs = 0; lsPeriodMaxUs = 0;
  lsWindowStartMs = nowMs;
}

// Нулира броячите за измерванията (по UART или по команда от главния модул)
void resetStats() {
  successPackets = 0; failedPackets = 0;
  dhtErrors = 0; cmdRejected = 0;
  safetyTrips = 0; safetyRefusals = 0;
  noInterrupts();
  btnEdges[0] = 0; btnEdges[1] = 0;
  interrupts();
  for (int b = 0; b < 2; b++) { btnEdgesAtLast[b] = 0; btnPresses[b] = 0; btnMaxEdges[b] = 0; }
  loopMaxEverUs = 0;
  minFreeHeap = ESP.getFreeHeap();
  lsCount = 0; lsSumUs = 0; lsMaxUs = 0; lsPeriodMaxUs = 0;
  lsWindowStartMs = millis();
  lsHavePrev = false;
}

// ===========================================================================
//  ЗАЩИТА ПРИ ОТЧЕТЕНА ВЛАГА
//  Блокира веднага; сваля блокировката едва след SAFETY_RELEASE_MS сухо.
// ===========================================================================

void handleSafetyCutoff() {
  unsigned long now = millis();
  if (digitalRead(HR202_PIN) == LOW) {
    lastWetMs = now;
    if (!isSafetyLocked) {
      isSafetyLocked = true;
      safetyTrips++;
      allRelaysOff();
      Serial.println(F("💦 Отчетена е влага - всички релета са изключени."));
      requestReport(REPORT_EVENT);
    }
  } else if (isSafetyLocked && now - lastWetMs >= SAFETY_RELEASE_MS) {
    isSafetyLocked = false;
    Serial.println(F("✅ Сухо е повече от 2 s - блокировката е свалена."));
    requestReport(REPORT_EVENT);
  }
}

// ===========================================================================
//  ФИЗИЧЕСКИ БУТОНИ - неблокиращо, без delay()
// ===========================================================================

// Извиква се при всяко прието превключване (натискане и отпускане):
// колко фронта е отчело прекъсването от предишното прието превключване.
void noteButtonTransition(uint8_t b) {
  uint32_t e = btnEdges[b];
  uint32_t d = e - btnEdgesAtLast[b];
  btnEdgesAtLast[b] = e;
  if (d > 255) d = 255;
  if (d > btnMaxEdges[b]) btnMaxEdges[b] = (uint8_t)d;
}

void handleButtons() {
  unsigned long now = millis();

  // --- Бутон 1 (D5): превключва реле 1 ---
  bool r = digitalRead(BUTTON_TOGGLE_PIN);
  if (r != toggleLastRead) { toggleLastRead = r; toggleLastChange = now; }
  else if (now - toggleLastChange > DEBOUNCE_MS && r != toggleStable) {
    toggleStable = r;
    noteButtonTransition(0);
    if (toggleStable == LOW) {                       // фронт на натискане
      btnPresses[0]++;
      if (applyRelay(0, !currentRelayState[0])) {
        Serial.print(F("🔘 Бутон D5 -> Реле 1: "));
        Serial.println(currentRelayState[0] ? F("ВКЛ") : F("ИЗКЛ"));
      } else {
        Serial.println(F("🔒 Бутонът е блокиран - отчетена е влага."));
      }
      requestReport(REPORT_EVENT);
    }
  }

  // --- Бутон 2 (D6): аварийно изключване на всички релета ---
  bool o = digitalRead(BUTTON_OFF_PIN);
  if (o != offLastRead) { offLastRead = o; offLastChange = now; }
  else if (now - offLastChange > DEBOUNCE_MS && o != offStable) {
    offStable = o;
    noteButtonTransition(1);
    if (offStable == LOW) {
      btnPresses[1]++;
      allRelaysOff();
      Serial.println(F("🛑 Аварийно изключване от бутона на D6."));
      requestReport(REPORT_EVENT);
    }
  }
}

// ===========================================================================
//  ИЗПЪЛНЕНИЕ НА ВХОДЯЩИТЕ КОМАНДИ (извън контекста на callback-а)
// ===========================================================================

void handleEspNowIncoming() {
  if (!commandPending) return;

  // Копие на данните - нова команда може да пристигне, докато тази се изпълнява
  uint8_t  type  = pendingCmdType;
  uint8_t  idx   = pendingRelayIndex;
  bool     state = pendingRelayCommand;
  uint16_t intv  = pendingInterval;
  float    off   = pendingVccOffset;
  uint8_t  ch    = pendingChannel;
  uint32_t seq   = pendingSeq;
  commandPending = false;
  ackSeq = seq;

  switch (type) {

    case CMD_RESTART:
      Serial.println(F("🔄 Получена команда за рестарт от главния модул. Рестартирам..."));
      delay(100);
      ESP.restart();
      break;

    case CMD_CONFIG: {
      bool needRestart = false;
      if (intv >= 500 && intv <= 60000)
        cfg.telemetryInterval = intv;
      if (off >= -2.0f && off <= 2.0f)
        cfg.vccOffset = off;
      if (ch >= 1 && ch <= 13 && ch != cfg.wifiChannel) {
        cfg.wifiChannel = ch;
        needRestart = true;
      }
      saveConfig();
      lastVcc = computeVcc(lastA0Sum, countActiveRelays());
      Serial.println(F("⚙️ Приета е конфигурация от главния модул."));
      if (needRestart) {
        Serial.println(F("🔄 Каналът е променен - рестартирам."));
        delay(200);
        ESP.restart();
      }
      requestReport(REPORT_COMMAND);
      break;
    }

    case CMD_PING:                       // само отговор - за измерване на закъснението
      requestReport(REPORT_COMMAND);
      break;

    case CMD_RESET_STATS:
      resetStats();
      Serial.println(F("🧹 Броячите за измерванията са нулирани от главния модул."));
      requestReport(REPORT_COMMAND);
      break;

    case CMD_RELAY:
      if (idx > 3) cmdRejected++;
      else         applyRelay(idx, state);   // при влага включването се отказва
      requestReport(REPORT_COMMAND);         // главният модул вижда реалното състояние
      break;

    default:
      cmdRejected++;
      break;
  }
}

// Съобщения по UART за отхвърлени пакети (най-много веднъж на 5 s)
void printLinkWarnings() {
  if (!foreignMacSeen && !badProtoSeen) return;
  if (linkWarnShown && millis() - lastLinkWarnMs < 5000) return;
  linkWarnShown = true;
  lastLinkWarnMs = millis();
  if (foreignMacSeen) {
    foreignMacSeen = false;
    Serial.println(F("⚠️ Отхвърлена е команда от непознат MAC адрес (проверете SET MAC)."));
  }
  if (badProtoSeen) {
    uint8_t v = badProtoSeen;
    badProtoSeen = 0;
    Serial.printf_P(PSTR("⚠️ Получена е команда с протокол v%u (очаква се v%u) - обновете и главния модул.\n"),
                    v, PROTO_VERSION);
  }
}

// ===========================================================================
//  UART - СОБСТВЕНО МЕНЮ НА ПОДЧИНЕНИЯ МОДУЛ
//  Възможно е, защото GPIO3 (RX) не е зает от бутон.
// ===========================================================================

void printUARTMenu() {
  Serial.println(F("\n=========================================="));
  Serial.println(F("       МЕНЮ НА ПОДЧИНЕНИЯ МОДУЛ (UART)    "));
  Serial.println(F("=========================================="));
  Serial.println(F("[1] ТЕКУЩ СТАТУС (датчици и релета)"));
  Serial.println(F("[2] ПУСНИ ВСИЧКИ РЕЛЕТА"));
  Serial.println(F("[3] СПРИ ВСИЧКИ РЕЛЕТА"));
  Serial.println(F("[4] РЕСТАРТИРАЙ МОДУЛА"));
  Serial.println(F("[5] ПОКАЖИ КОНФИГУРАЦИЯТА"));
  Serial.println(F("[6] ИЗМЕРВАНИЯ (цикъл, памет, бутони, A0)"));
  Serial.println(F("[M] ТОВА МЕНЮ"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("Отделно реле:  R1 ON | R2 OFF | ... | R4 ON"));
  Serial.println(F("RESET STATS    нулира броячите за измерванията"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("КОНФИГУРАЦИЯ (влиза в сила след SAVE и рестарт):"));
  Serial.println(F("  SET MAC <xx:xx:..>       MAC на главния модул"));
  Serial.println(F("  SET CHANNEL <1-13>       Wi-Fi канал"));
  Serial.println(F("  SET INTERVAL <500-60000> интервал на телеметрията, ms"));
  Serial.println(F("  SET VCCOFFSET <±V>       корекция на измереното VCC"));
  Serial.println(F("  SAVE       запис в EEPROM"));
  Serial.println(F("  FACTORY    фабрични настройки + рестарт"));
  Serial.println(F("==========================================\n"));
}

void printConfig() {
  char macBuf[18];
  macToStr(cfg.peerMac, macBuf);
  Serial.println(F("\n--- КОНФИГУРАЦИЯ НА ПОДЧИНЕНИЯ МОДУЛ ---"));
  Serial.print(F("Собствен MAC      : ")); Serial.println(WiFi.macAddress());
  Serial.print(F("MAC на главния    : ")); Serial.println(macBuf);
  Serial.print(F("Wi-Fi канал       : ")); Serial.println(cfg.wifiChannel);
  Serial.print(F("Интервал телем.   : ")); Serial.print(cfg.telemetryInterval); Serial.println(F(" ms"));
  Serial.print(F("Корекция VCC      : ")); Serial.print(cfg.vccOffset, 2); Serial.println(F(" V"));
  Serial.print(F("Криптиране ESP-NOW: ")); Serial.println(USE_ESPNOW_ENCRYPTION ? F("ВКЛЮЧЕНО") : F("ИЗКЛЮЧЕНО"));
  Serial.print(F("Версия фърмуер    : ")); Serial.println(FW_VERSION);
  Serial.println(F("----------------------------------------\n"));
}

void printStatus() {
  Serial.println(F("\n--- ТЕКУЩ СТАТУС ---"));
  Serial.print(F("Температура    : "));
  if (haveValidReading) { Serial.print(lastValidTemp); Serial.println(F(" °C")); }
  else                    Serial.println(F("няма валидно четене"));
  Serial.print(F("Влажност       : "));
  if (haveValidReading) { Serial.print(lastValidHum); Serial.println(F(" %")); }
  else                    Serial.println(F("няма валидно четене"));
  Serial.print(F("HR202          : ")); Serial.println(digitalRead(HR202_PIN) ? F("СУХО") : F("МОКРО"));
  Serial.print(F("Блокировка     : ")); Serial.println(isSafetyLocked ? F("АКТИВНА") : F("няма"));
  Serial.print(F("Релета         : "));
  for (int i = 0; i < 4; i++) {
    Serial.print(F("[R")); Serial.print(i + 1); Serial.print(F(": "));
    Serial.print(digitalRead(relayPins[i]) == HIGH ? F("ВКЛ") : F("ИЗКЛ")); Serial.print(F("] "));
  }
  Serial.println();
  Serial.print(F("Успешни пакети : ")); Serial.println(successPackets);
  Serial.print(F("Загубени пакети: ")); Serial.println(failedPackets);
  Serial.print(F("Грешки DHT11   : ")); Serial.println(dhtErrors);
  Serial.print(F("Свободна RAM   : ")); Serial.print(ESP.getFreeHeap()); Serial.println(F(" B"));
  Serial.print(F("VCC            : ")); Serial.print(lastVcc, 2); Serial.println(F(" V"));
  Serial.print(F("Цикъл (ср/макс): ")); Serial.print(pubLoopAvgUs); Serial.print(F(" / "));
  Serial.print(pubLoopMaxUs); Serial.println(F(" µs"));
  Serial.print(F("Време на работа: ")); Serial.print(millis() / 1000); Serial.println(F(" s"));
  Serial.print(F("Причина рестарт: ")); Serial.println(systemResetReason);
  Serial.println(F("--------------------\n"));
}

void printMeasurements() {
  Serial.println(F("\n--- ИЗМЕРВАНИЯ ---"));
  Serial.print(F("Прозорец             : ")); Serial.print(pubWindowMs); Serial.print(F(" ms, "));
  Serial.print(pubLoopCount); Serial.println(F(" итерации"));
  Serial.print(F("Изпълнение ср./макс. : ")); Serial.print(pubLoopAvgUs); Serial.print(F(" / "));
  Serial.print(pubLoopMaxUs); Serial.println(F(" µs"));
  Serial.print(F("Период макс.         : ")); Serial.print(pubPeriodMaxUs); Serial.println(F(" µs"));
  Serial.print(F("Изпълнение макс. общо: ")); Serial.print(loopMaxEverUs); Serial.println(F(" µs"));
  uint32_t hfree, hmax; uint8_t hfrag;
  ESP.getHeapStats(&hfree, &hmax, &hfrag);
  Serial.print(F("Памет своб./мин.     : ")); Serial.print(hfree); Serial.print(F(" / "));
  Serial.print(minFreeHeap); Serial.println(F(" B"));
  Serial.print(F("Най-голям блок/фрагм.: ")); Serial.print(hmax); Serial.print(F(" B / "));
  Serial.print(hfrag); Serial.println(F(" %"));
  Serial.print(F("A0 (сума от 10 проби): ")); Serial.print(lastA0Sum);
  Serial.print(F("  -> средно ")); Serial.println(lastA0Sum / (float)VCC_SAMPLES, 1);
  Serial.print(F("VCC (включени релета): ")); Serial.print(lastVcc, 3); Serial.print(F(" V ("));
  Serial.print(countActiveRelays()); Serial.println(F(")"));
  for (int b = 0; b < 2; b++) {
    Serial.print(b == 0 ? F("Бутон D5             : ") : F("Бутон D6             : "));
    Serial.print(btnPresses[b]); Serial.print(F(" натискания, "));
    Serial.print(btnEdges[b]); Serial.print(F(" фронта, най-много "));
    Serial.print(btnMaxEdges[b]); Serial.println(F(" за едно превключване"));
  }
  Serial.print(F("Защита: задействания : ")); Serial.print(safetyTrips);
  Serial.print(F(", откази: ")); Serial.println(safetyRefusals);
  Serial.print(F("Отхвърлени команди   : ")); Serial.println(cmdRejected);
  Serial.print(F("Последна команда №   : ")); Serial.println(ackSeq);
  Serial.println(F("------------------\n"));
}

void handleSerialCommands() {
  if (Serial.available() <= 0) return;

  String raw = Serial.readStringUntil('\n');
  raw.trim();
  if (raw.length() == 0) return;

  String up = raw;
  up.toUpperCase();

  Serial.print(F("\n⌨️ Въведено: "));
  Serial.println(raw);

  // ---------- Конфигурационни команди ----------
  if (up.startsWith("SET ")) {
    int sp = raw.indexOf(' ', 4);
    String key = (sp < 0 ? raw.substring(4) : raw.substring(4, sp));
    String val = (sp < 0 ? String("")       : raw.substring(sp + 1));
    key.trim(); key.toUpperCase(); val.trim();

    if (val.length() == 0) { Serial.println(F("❌ Липсва стойност. Пример: SET CHANNEL 6")); return; }

    if (key == "MAC") {
      uint8_t m[6];
      if (parseMac(val, m)) {
        char macBuf[18];
        memcpy(cfg.peerMac, m, 6);
        macToStr(cfg.peerMac, macBuf);
        Serial.print(F("✅ MAC на главния модул: ")); Serial.println(macBuf);
        Serial.println(F("   SAVE + рестарт (4)."));
      } else Serial.println(F("❌ Невалиден формат. Пример: SET MAC A4:E5:7C:01:E6:9E"));
    }
    else if (key == "CHANNEL") {
      int c = val.toInt();
      if (c >= 1 && c <= 13) {
        cfg.wifiChannel = (uint8_t)c;
        Serial.print(F("✅ Канал: ")); Serial.println(c);
        Serial.println(F("⚠️ Трябва да съвпада с канала на рутера! SAVE + рестарт (4)."));
      } else Serial.println(F("❌ Допустим диапазон: 1-13."));
    }
    else if (key == "INTERVAL") {
      long v = val.toInt();
      if (v >= 500 && v <= 60000) {
        cfg.telemetryInterval = (uint16_t)v;
        Serial.print(F("✅ Интервал: ")); Serial.print(v); Serial.println(F(" ms (влиза в сила веднага). SAVE."));
      } else Serial.println(F("❌ Допустим диапазон: 500-60000 ms."));
    }
    else if (key == "VCCOFFSET") {
      float v = val.toFloat();
      if (v >= -2.0 && v <= 2.0) {
        cfg.vccOffset = v;
        lastVcc = computeVcc(lastA0Sum, countActiveRelays());
        Serial.print(F("✅ Корекция на VCC: ")); Serial.print(v, 2); Serial.println(F(" V (влиза в сила веднага). SAVE."));
      } else Serial.println(F("❌ Допустим диапазон: -2.00 до +2.00 V."));
    }
    else Serial.println(F("❓ Непознат параметър. Натиснете M за списъка."));
    return;
  }

  if (up == "SAVE")    { saveConfig(); return; }
  if (up == "FACTORY") {
    Serial.println(F("♻️ Възстановявам фабричните настройки..."));
    setDefaults(); saveConfig(); delay(200); ESP.restart(); return;
  }

  // ---------- Оперативни команди ----------
  if (up == "M" || up == "MENU" || up == "HELP") { printUARTMenu();     return; }
  if (up == "5" || up == "CONFIG")               { printConfig();       return; }
  if (up == "1" || up == "STATUS")               { printStatus();       return; }
  if (up == "6" || up == "STATS")                { printMeasurements(); return; }
  if (up == "RESET STATS") {
    resetStats();
    Serial.println(F("🧹 Броячите за измерванията са нулирани."));
    requestReport(REPORT_EVENT);
    return;
  }

  if (up == "2" || up == "ALL ON") {
    if (isSafetyLocked) { safetyRefusals++; Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
    for (int i = 0; i < 4; i++) applyRelay(i, true);
    Serial.println(F("✅ Всички релета са пуснати."));
    requestReport(REPORT_EVENT);
    return;
  }
  if (up == "3" || up == "ALL OFF") {
    allRelaysOff();
    Serial.println(F("✅ Всички релета са изключени."));
    requestReport(REPORT_EVENT);
    return;
  }
  if (up == "4" || up == "RESTART") {
    Serial.println(F("🔄 Рестартиране на модула..."));
    delay(150); ESP.restart(); return;
  }

  // R<n> ON / OFF
  if (up.startsWith("R") && up.length() >= 5) {
    int relayNum = up.substring(1, 2).toInt() - 1;
    String action = up.substring(3); action.trim();
    if (relayNum >= 0 && relayNum < 4 && (action == "ON" || action == "OFF")) {
      bool target = (action == "ON");
      if (applyRelay(relayNum, target)) {
        Serial.print(F("✅ Реле ")); Serial.print(relayNum + 1);
        Serial.println(target ? F(" ВКЛЮЧЕНО") : F(" ИЗКЛЮЧЕНО"));
        requestReport(REPORT_EVENT);
      } else {
        Serial.println(F("⚠️ Отказано - отчетена е влага."));
      }
      return;
    }
    Serial.println(F("❌ Формат: R1 ON, R2 OFF ... R4 ON"));
    return;
  }

  Serial.println(F("❓ Непозната команда. Натиснете 'M' за менюто."));
}

// ===========================================================================
//  ТЕЛЕМЕТРИЯ
// ===========================================================================

// Четене на DHT11 - блокира ~26 ms (библиотеката DHT: 1 ms + 20 ms стартов
// импулс чрез delay() и ~5 ms приемане на 40 бита при забранени прекъсвания).
void readDhtIfDue(unsigned long now) {
  if (dhtEverRead && now - lastDhtRead < DHT_MIN_INTERVAL_MS) return;
  lastDhtRead = now;
  dhtEverRead = true;

  bool  ok = dht.read(true);          // ново четене от датчика
  float h  = dht.readHumidity();      // от току-що прочетените данни
  float t  = dht.readTemperature();

  // При неуспешно четене се задържа последната валидна стойност,
  // а не се записва 0.0 - иначе панелът показва подвеждащи 0 °C.
  if (!ok || isnan(t) || isnan(h)) {
    dhtErrors++;
  } else {
    lastValidTemp = t; lastValidHum = h; haveValidReading = true;
  }
}

void sendReport(uint8_t type) {
  struct_message &m = outgoingTelemetry;
  memset(&m, 0, sizeof(m));

  m.protoVersion = PROTO_VERSION;
  m.fwVersion    = FW_VERSION;
  m.reportType   = type;
  m.cfgChannel   = cfg.wifiChannel;
  m.readingId    = ++readingCounter;
  m.ackSeq       = ackSeq;

  m.temperature  = haveValidReading ? lastValidTemp : 0.0;
  m.humidity     = haveValidReading ? lastValidHum  : 0.0;
  m.dhtValid     = haveValidReading;
  m.hr202State   = (digitalRead(HR202_PIN) == HIGH);
  m.safetyLocked = isSafetyLocked;

  // Реалното състояние се чете обратно от изходните пинове
  for (int i = 0; i < 4; i++)
    m.relayState[i] = (digitalRead(relayPins[i]) == HIGH);

  m.dhtErrors    = dhtErrors;
  m.cfgInterval  = cfg.telemetryInterval;
  m.cfgVccOffset = cfg.vccOffset;
  m.vcc          = lastVcc;
  m.a0Sum        = lastA0Sum;
  m.uptime       = millis() / 1000;
  strncpy(m.resetReason, systemResetReason, sizeof(m.resetReason) - 1);

  m.successPackets = successPackets;
  m.failedPackets  = failedPackets;

  m.loopCount     = pubLoopCount;
  m.loopAvgUs     = pubLoopAvgUs;
  m.loopMaxUs     = pubLoopMaxUs;
  m.periodMaxUs   = pubPeriodMaxUs;
  m.windowMs      = pubWindowMs;
  m.loopMaxEverUs = loopMaxEverUs;

  uint32_t hfree, hmax; uint8_t hfrag;
  ESP.getHeapStats(&hfree, &hmax, &hfrag);
  if (hfree < minFreeHeap) minFreeHeap = hfree;
  m.freeHeap     = hfree;
  m.minFreeHeap  = minFreeHeap;
  m.maxFreeBlock = hmax;
  m.heapFrag     = hfrag;

  for (int b = 0; b < 2; b++) {
    m.btnEdges[b]    = btnEdges[b];
    m.btnPresses[b]  = btnPresses[b];
    m.btnMaxEdges[b] = btnMaxEdges[b];
  }
  m.cmdRejected    = cmdRejected;
  m.safetyTrips    = safetyTrips;
  m.safetyRefusals = safetyRefusals;

  lastReportMs = millis();
  esp_now_send(cfg.peerMac, (uint8_t *)&m, sizeof(m));
}

// Периодичната телеметрия е в две стъпки, за да не блокира loop():
//   1) при настъпване на интервала - четене на DHT11 и старт на измерването на VCC;
//   2) след последната проба на A0 - затваряне на прозореца и изпращане.
// Между тях се изпращат и незабавните отчети след команди и събития.
void handleTelemetry() {
  unsigned long now = millis();

  if (!vccBusy && now - lastTelemetrySend >= cfg.telemetryInterval) {
    lastTelemetrySend = now;
    readDhtIfDue(now);
    startVccBurst();
  }

  if (serviceVccBurst()) {
    publishLoopWindow();
    sendReport(REPORT_PERIODIC);
    reportPending = false;             // периодичният пакет носи и последната промяна
    return;
  }

  if (reportPending &&
      (reportPendingType == REPORT_COMMAND || millis() - lastReportMs >= REPORT_MIN_GAP_MS)) {
    reportPending = false;
    sendReport(reportPendingType);
  }
}

// ===========================================================================
//  SETUP
// ===========================================================================

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);           // не блокира loop() при непълен ред

  // Релетата се поставят в изключено състояние ПРЕДИ конфигуриране на пиновете,
  // за да няма кратък импулс при включване на захранването.
  for (int i = 0; i < 4; i++) {
    digitalWrite(relayPins[i], LOW);
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  pinMode(BUTTON_TOGGLE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_OFF_PIN,    INPUT_PULLUP);
  pinMode(HR202_PIN,         INPUT_PULLUP);

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  // --- Фабрично нулиране: задържан бутон на D5 при стартиране ---
  if (digitalRead(BUTTON_TOGGLE_PIN) == LOW) {
    Serial.println(F("\n⏳ Задръжте бутона още 3 секунди за фабрично нулиране..."));
    unsigned long t0 = millis();
    bool held = true;
    while (millis() - t0 < 3000) {
      if (digitalRead(BUTTON_TOGGLE_PIN) == HIGH) { held = false; break; }
      delay(50);
    }
    if (held) {
      Serial.println(F("♻️ Фабрично нулиране на конфигурацията!"));
      setDefaults();
      saveConfig();
      delay(300);
      ESP.restart();
    } else {
      Serial.println(F("   Отказано."));
    }
  }

  dht.begin();
  strncpy(systemResetReason, ESP.getResetReason().c_str(), sizeof(systemResetReason) - 1);

  // Прекъсванията само броят фронтовете; логиката на бутоните е в loop()
  attachInterrupt(digitalPinToInterrupt(BUTTON_TOGGLE_PIN), isrButtonToggle, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BUTTON_OFF_PIN),    isrButtonOff,    CHANGE);

  // Първо измерване на VCC (~20 ms), за да има стойност още в първия отчет
  startVccBurst();
  while (!serviceVccBurst()) delay(1);

  // Скритият softAP служи единствено за заключване на Wi-Fi канала,
  // така че той да съвпада с канала, на който работи главният модул.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Hidden_Slave", "", cfg.wifiChannel, 1);

  if (esp_now_init() != 0) { Serial.println(F("❌ Грешка при инициализация на ESP-NOW!")); return; }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

#if USE_ESPNOW_ENCRYPTION
  esp_now_set_kok(kokKey, 16);
  esp_now_add_peer(cfg.peerMac, ESP_NOW_ROLE_COMBO, cfg.wifiChannel, lmkKey, 16);
#else
  esp_now_add_peer(cfg.peerMac, ESP_NOW_ROLE_COMBO, cfg.wifiChannel, NULL, 0);
#endif

  Serial.println(F("\n--- ПОДЧИНЕНИЯТ МОДУЛ Е СТАРТИРАН ---"));
  Serial.print(F("Собствен MAC   : ")); Serial.println(WiFi.macAddress());
  Serial.print(F("Причина рестарт: ")); Serial.println(systemResetReason);
  Serial.print(F("Фърмуер / протокол: v")); Serial.print(FW_VERSION);
  Serial.print(F(" / v")); Serial.println(PROTO_VERSION);
  printUARTMenu();

  minFreeHeap = ESP.getFreeHeap();
  lsWindowStartMs = millis();
}

// ===========================================================================
//  LOOP
// ===========================================================================

void loop() {
  uint32_t t0 = micros();
  if (lsHavePrev) {                                   // период между две итерации
    uint32_t period = t0 - lsPrevStartUs;
    if (period > lsPeriodMaxUs) lsPeriodMaxUs = period;
  }
  lsPrevStartUs = t0;
  lsHavePrev = true;

  handleSafetyCutoff();     // защита от влага
  handleButtons();          // физически бутони (неблокиращо)
  handleEspNowIncoming();   // команди по мрежата: реле / рестарт / конфигурация / тест
  handleSerialCommands();   // локално UART меню
  handleTelemetry();        // DHT11, VCC и изпращане на отчетите
  printLinkWarnings();      // съобщения за отхвърлени пакети

  uint32_t fh = ESP.getFreeHeap();
  if (fh < minFreeHeap) minFreeHeap = fh;

  uint32_t dt = micros() - t0;                        // време за изпълнение
  lsCount++;
  lsSumUs += dt;
  if (dt > lsMaxUs)       lsMaxUs = dt;
  if (dt > loopMaxEverUs) loopMaxEverUs = dt;
}

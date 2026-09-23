/*
 * ============================================================================
 *  СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС
 *  ПОДЧИНЕН МОДУЛ (SLAVE)
 *  ----------------------------------------------------------------------------
 *  Дипломна работа - ТУ София, ФЕТТ, катедра "Електронна техника"
 *  Дипломант: инж. Кръстиян Тодоров Праматаров, ф. № 901322003
 *  Ръководител: доц. д-р инж. Любомир Богданов
 *  ----------------------------------------------------------------------------
 *  Версия 2.1 - добавени:
 *    - СОБСТВЕНО UART МЕНЮ за конфигуриране и управление (RX/GPIO3 е освободен)
 *    - Бутонът за аварийно изключване е преместен от GPIO3 на D6 (GPIO12)
 *    - Конфигурация в EEPROM, задавана локално по UART или дистанционно
 *    - Опционално криптиране на ESP-NOW и отхвърляне на дублирани пакети
 *    - Неблокиращо отстраняване на трептенето на бутоните (без delay())
 *    - Задържане на последната валидна стойност при грешка от DHT11
 *    - Фабрично нулиране: задръжте бутона на D5 при подаване на захранване
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

#define FW_VERSION    2
#define PROTO_VERSION 2

// ---------------------------------------------------------------------------
//  ПИНОВЕ
//  ВАЖНО: GPIO3 (RX) вече НЕ се използва за бутон - серийният порт е свободен.
//  Бутонът за аварийно изключване е преместен на D6 (GPIO12).
//  Не използвайте D8 (GPIO15) за бутон с вътрешен pull-up - GPIO15 трябва да е
//  на ниско ниво при стартиране, иначе платката не зарежда.
// ---------------------------------------------------------------------------
const uint8_t relayPins[4] = {D0, D1, D2, D3};  // изходи за четирите релета
#define DHTPIN            D4    // датчик за температура и влажност
#define BUTTON_TOGGLE_PIN D5    // бутон 1: превключва реле 1
#define BUTTON_OFF_PIN    D6    // бутон 2: аварийно изключване
#define HR202_PIN         D7    // датчик за наличие на влага
#define DHTTYPE           DHT11

DHT dht(DHTPIN, DHTTYPE);

// ===========================================================================
//  СТРУКТУРИ НА ОБМЕНА  (ЗАДЪЛЖИТЕЛНО ИДЕНТИЧНИ С MASTER.INO!)
// ===========================================================================

typedef struct struct_message {
    uint8_t  protoVersion;
    int      readingId;
    float    temperature;
    float    humidity;
    bool     relayState[4];
    bool     hr202State;
    uint32_t freeHeap;
    float    vcc;
    uint32_t loopTime;
    uint32_t uptime;
    char     resetReason[32];
    uint32_t successPackets;
    uint32_t failedPackets;
    uint16_t dhtErrors;
    uint16_t cfgInterval;
    float    cfgVccOffset;
    uint8_t  cfgChannel;
    uint8_t  fwVersion;
} struct_message;

#define CMD_RELAY   0
#define CMD_RESTART 1
#define CMD_CONFIG  2

typedef struct struct_control {
    uint8_t  protoVersion;
    uint8_t  cmdType;
    uint8_t  relayIndex;
    bool     relayState;
    uint16_t telemetryInterval;
    float    vccOffset;
    uint8_t  wifiChannel;
    uint32_t seq;
} struct_control;

struct_message outgoingTelemetry;
struct_control incomingControl;

// ===========================================================================
//  КОНФИГУРАЦИЯ В EEPROM
// ===========================================================================

#define CFG_MAGIC   0x484D5331UL   // "HMS1"
#define CFG_VERSION 2
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
int  counter = 0;
bool currentRelayState[4] = {false, false, false, false};
bool isSafetyLocked = false;

volatile bool commandPending = false;
volatile uint8_t  pendingCmdType     = CMD_RELAY;
volatile uint8_t  pendingRelayIndex  = 0;
volatile bool     pendingRelayCommand = false;
volatile uint16_t pendingInterval    = 2000;
volatile float    pendingVccOffset   = 0.30;
volatile uint8_t  pendingChannel     = 6;
uint32_t lastSeq = 0;

uint32_t lastLoopDuration = 0;
String   systemResetReason = "";
uint32_t successPackets = 0, failedPackets = 0;
uint16_t dhtErrors = 0;
float    lastValidTemp = 0.0, lastValidHum = 0.0;
bool     haveValidReading = false;

unsigned long lastTelemetrySend = 0;

// Неблокиращо отстраняване на трептенето (заменя delay() в цикъла)
#define DEBOUNCE_MS 60
bool toggleStable = HIGH, toggleLastRead = HIGH;
bool offStable = HIGH,    offLastRead = HIGH;
unsigned long toggleLastChange = 0, offLastChange = 0;

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
  cfg.vccOffset         = 0.30;
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
  if (cfg.magic != CFG_MAGIC || cfg.version != CFG_VERSION || cfg.crc != calc) {
    Serial.println(F("⚠️ Няма валидна конфигурация - зареждам фабричните настройки."));
    setDefaults();
    saveConfig();
  } else {
    Serial.println(F("✅ Конфигурацията е прочетена от EEPROM."));
  }
}

String macToString(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool parseMac(const String &s, uint8_t *out) {
  unsigned int b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
    return false;
  for (int i = 0; i < 6; i++) { if (b[i] > 255) return false; out[i] = (uint8_t)b[i]; }
  return true;
}

// ===========================================================================
//  УПРАВЛЕНИЕ НА РЕЛЕТАТА (обща точка за бутони, UART и ESP-NOW)
// ===========================================================================

bool applyRelay(uint8_t idx, bool state) {
  if (idx > 3) return false;
  if (isSafetyLocked && state) return false;      // включване при влага е забранено
  currentRelayState[idx] = state;
  digitalWrite(relayPins[idx], state ? HIGH : LOW);
  return true;
}

// ===========================================================================
//  ESP-NOW CALLBACKS
//  Вътре в callback НЕ се пишат Serial.print и НЕ се прави тежка обработка.
// ===========================================================================

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) successPackets++;
  else                 failedPackets++;
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len != sizeof(incomingControl)) return;
  memcpy(&incomingControl, incomingData, sizeof(incomingControl));

  if (incomingControl.protoVersion != PROTO_VERSION) return;   // друга версия
  if (incomingControl.seq == lastSeq) return;                  // дублиран пакет
  lastSeq = incomingControl.seq;

  pendingCmdType      = incomingControl.cmdType;
  pendingRelayIndex   = incomingControl.relayIndex;
  pendingRelayCommand = incomingControl.relayState;
  pendingInterval     = incomingControl.telemetryInterval;
  pendingVccOffset    = incomingControl.vccOffset;
  pendingChannel      = incomingControl.wifiChannel;
  commandPending      = true;
}

// ===========================================================================
//  ЗАЩИТА ПРИ ОТЧЕТЕНА ВЛАГА
// ===========================================================================

void handleSafetyCutoff() {
  if (digitalRead(HR202_PIN) == LOW) {
    if (!isSafetyLocked) {
      isSafetyLocked = true;
      for (int i = 0; i < 4; i++) {
        currentRelayState[i] = false;
        digitalWrite(relayPins[i], LOW);
      }
      Serial.println(F("💦 Отчетена е влага - всички релета са изключени."));
    }
  } else {
    if (isSafetyLocked) Serial.println(F("✅ Влагата изчезна - блокировката е свалена."));
    isSafetyLocked = false;
  }
}

// ===========================================================================
//  ФИЗИЧЕСКИ БУТОНИ - неблокиращо, без delay()
// ===========================================================================

void handleButtons() {
  unsigned long now = millis();

  // --- Бутон 1 (D5): превключва реле 1 ---
  bool r = digitalRead(BUTTON_TOGGLE_PIN);
  if (r != toggleLastRead) { toggleLastRead = r; toggleLastChange = now; }
  else if (now - toggleLastChange > DEBOUNCE_MS && r != toggleStable) {
    toggleStable = r;
    if (toggleStable == LOW) {                       // фронт на натискане
      if (applyRelay(0, !currentRelayState[0])) {
        Serial.print(F("🔘 Бутон D5 -> Реле 1: "));
        Serial.println(currentRelayState[0] ? F("ВКЛ") : F("ИЗКЛ"));
      } else {
        Serial.println(F("🔒 Бутонът е блокиран - отчетена е влага."));
      }
    }
  }

  // --- Бутон 2 (D6): аварийно изключване на всички релета ---
  bool o = digitalRead(BUTTON_OFF_PIN);
  if (o != offLastRead) { offLastRead = o; offLastChange = now; }
  else if (now - offLastChange > DEBOUNCE_MS && o != offStable) {
    offStable = o;
    if (offStable == LOW) {
      for (int i = 0; i < 4; i++) {
        currentRelayState[i] = false;
        digitalWrite(relayPins[i], LOW);
      }
      Serial.println(F("🛑 Аварийно изключване от бутона на D6."));
    }
  }
}

// ===========================================================================
//  ИЗПЪЛНЕНИЕ НА ВХОДЯЩИТЕ КОМАНДИ (извън контекста на прекъсването)
// ===========================================================================

void handleEspNowIncoming() {
  if (!commandPending) return;
  commandPending = false;

  switch (pendingCmdType) {

    case CMD_RESTART:
      Serial.println(F("🔄 Получена команда за рестарт от главния модул. Рестартирам..."));
      delay(100);
      ESP.restart();
      break;

    case CMD_CONFIG: {
      bool needRestart = false;
      if (pendingInterval >= 500 && pendingInterval <= 60000)
        cfg.telemetryInterval = pendingInterval;
      if (pendingVccOffset >= -2.0 && pendingVccOffset <= 2.0)
        cfg.vccOffset = pendingVccOffset;
      if (pendingChannel >= 1 && pendingChannel <= 13 && pendingChannel != cfg.wifiChannel) {
        cfg.wifiChannel = pendingChannel;
        needRestart = true;
      }
      saveConfig();
      Serial.println(F("⚙️ Приета е конфигурация от главния модул."));
      if (needRestart) {
        Serial.println(F("🔄 Каналът е променен - рестартирам."));
        delay(200);
        ESP.restart();
      }
      break;
    }

    case CMD_RELAY:
    default:
      applyRelay(pendingRelayIndex, pendingRelayCommand);
      break;
  }
}

// ===========================================================================
//  UART - СОБСТВЕНО МЕНЮ НА ПОДЧИНЕНИЯ МОДУЛ
//  Възможно е, защото GPIO3 (RX) вече не е зает от бутон.
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
  Serial.println(F("[M] ТОВА МЕНЮ"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("Отделно реле:  R1 ON | R2 OFF | ... | R4 ON"));
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
  Serial.println(F("\n--- КОНФИГУРАЦИЯ НА ПОДЧИНЕНИЯ МОДУЛ ---"));
  Serial.print(F("Собствен MAC      : ")); Serial.println(WiFi.macAddress());
  Serial.print(F("MAC на главния    : ")); Serial.println(macToString(cfg.peerMac));
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
  Serial.print(F("HR202          : ")); Serial.println(digitalRead(HR202_PIN) ? F("СУХО") : F("МОКРО (блокирано)"));
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
  Serial.print(F("VCC            : ")); Serial.print((ESP.getVcc() / 1000.0) + cfg.vccOffset, 2); Serial.println(F(" V"));
  Serial.print(F("Време на цикъла: ")); Serial.print(lastLoopDuration); Serial.println(F(" ms"));
  Serial.print(F("Време на работа: ")); Serial.print(millis() / 1000); Serial.println(F(" s"));
  Serial.print(F("Причина рестарт: ")); Serial.println(systemResetReason);
  Serial.println(F("--------------------\n"));
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
        memcpy(cfg.peerMac, m, 6);
        Serial.print(F("✅ MAC на главния модул: ")); Serial.println(macToString(cfg.peerMac));
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
        Serial.print(F("✅ Корекция на VCC: ")); Serial.print(v, 2); Serial.println(F(" V. SAVE."));
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
  if (up == "M" || up == "MENU" || up == "HELP") { printUARTMenu(); return; }
  if (up == "5" || up == "CONFIG")               { printConfig();   return; }
  if (up == "1" || up == "STATUS")               { printStatus();   return; }

  if (up == "2" || up == "ALL ON") {
    if (isSafetyLocked) { Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
    for (int i = 0; i < 4; i++) applyRelay(i, true);
    Serial.println(F("✅ Всички релета са пуснати."));
    return;
  }
  if (up == "3" || up == "ALL OFF") {
    for (int i = 0; i < 4; i++) applyRelay(i, false);
    Serial.println(F("✅ Всички релета са изключени."));
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

void sendTelemetryIfDue() {
  unsigned long now = millis();
  if (now - lastTelemetrySend < cfg.telemetryInterval) return;
  lastTelemetrySend = now;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // При неуспешно четене се задържа последната валидна стойност,
  // а не се записва 0.0 - иначе панелът показва подвеждащи 0 °C.
  if (isnan(t) || isnan(h)) {
    dhtErrors++;
  } else {
    lastValidTemp = t; lastValidHum = h; haveValidReading = true;
  }

  outgoingTelemetry.protoVersion = PROTO_VERSION;
  outgoingTelemetry.temperature  = haveValidReading ? lastValidTemp : 0.0;
  outgoingTelemetry.humidity     = haveValidReading ? lastValidHum  : 0.0;
  outgoingTelemetry.hr202State   = digitalRead(HR202_PIN);


  // Реалното състояние се чете обратно от изходните пинове
  for (int i = 0; i < 4; i++)
    outgoingTelemetry.relayState[i] = (digitalRead(relayPins[i]) == HIGH);

// ИЗМЕРВАНЕ НА A0 С ОСРЕДНЯВАНЕ НА 10 ПРОБИ ЗА ИЗГЛАЖДАНЕ НА ШУМА
  long sumA0 = 0;
  for(int i = 0; i < 10; i++) {
    int rawValue = analogRead(A0);
    sumA0 += rawValue;
    delay(2);
  }
  float avgA0 = sumA0 / 10.0;

  // 2. БРОЕНЕ НА ВКЛЮЧЕНИТЕ РЕЛЕТА
  int activeRelaysCount = 0;
  for (int i = 0; i < 4; i++) {
    if (currentRelayState[i] == true) { 
      activeRelaysCount++;
    }
  }
  // 3. БАЗОВА СМЕТКА С ТВОЯ КАЛИБРИРАН МНОЖИТЕЛ
  float baseVcc = (avgA0 / 1024.0) * 10.91;

  // 4. IF-ELSE ЛОГИКА (В покой спрямо Под товар)
  if (activeRelaysCount == 0)
  {
    // РЕЖИМ 1: Всички релета са спрени.
    // Показваме чистата сметка без никакви софтуерни манипулации.
  outgoingTelemetry.vcc = baseVcc;
  }
  else
  {
    // РЕЖИМ 2: Поне едно реле е пуснато.
    // Изваждаме изкуствената грешка на чипа за всяко пуснато реле.
    // Нагласи 0.055 според това, което показва мултицетът при 1 реле.
    float errorPerRelay = 0.055; 
    
    outgoingTelemetry.vcc = baseVcc - (activeRelaysCount * errorPerRelay);
  }
  outgoingTelemetry.freeHeap = ESP.getFreeHeap();
  outgoingTelemetry.loopTime = lastLoopDuration;
  outgoingTelemetry.uptime   = millis() / 1000;
  strncpy(outgoingTelemetry.resetReason, systemResetReason.c_str(),
          sizeof(outgoingTelemetry.resetReason) - 1);
  outgoingTelemetry.resetReason[sizeof(outgoingTelemetry.resetReason) - 1] = 0;

  outgoingTelemetry.successPackets = successPackets;
  outgoingTelemetry.failedPackets  = failedPackets;
  outgoingTelemetry.dhtErrors      = dhtErrors;
  outgoingTelemetry.cfgInterval    = cfg.telemetryInterval;
  outgoingTelemetry.cfgVccOffset   = cfg.vccOffset;
  outgoingTelemetry.cfgChannel     = cfg.wifiChannel;
  outgoingTelemetry.fwVersion      = FW_VERSION;

  counter++;
  outgoingTelemetry.readingId = counter;

  esp_now_send(cfg.peerMac, (uint8_t *)&outgoingTelemetry, sizeof(outgoingTelemetry));
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
  systemResetReason = ESP.getResetReason();

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
  printUARTMenu();
}

// ===========================================================================
//  LOOP
// ===========================================================================

void loop() {
  unsigned long loopStart = millis();

  handleSafetyCutoff();     // защита от влага
  handleButtons();          // физически бутони (неблокиращо)
  handleEspNowIncoming();   // команди по мрежата: реле / рестарт / конфигурация
  handleSerialCommands();   // локално UART меню
  sendTelemetryIfDue();     // изпращане на телеметрия

  lastLoopDuration = millis() - loopStart;
}

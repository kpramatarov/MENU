/*
 * ============================================================================
 *  СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС
 *  ГЛАВЕН МОДУЛ (MASTER / ХЪБ)
 *  ----------------------------------------------------------------------------
 *  Дипломна работа - ТУ София, ФЕТТ, катедра "Електронна техника"
 *  Дипломант: инж. Кръстиян Тодоров Праматаров, ф. № 901322003
 *  Ръководител: доц. д-р инж. Любомир Богданов
 *  ----------------------------------------------------------------------------
 *  Версия 2.0 - добавени:
 *    - Пълна конфигурация по UART със запис в EEPROM (т.5 от заданието)
 *    - Отдалечено конфигуриране на подчинения модул през ESP-NOW
 *    - Защита на достъпа: парола за OTA и HTTP Basic автентикация
 *    - Опционално криптиране на ESP-NOW и пореден номер срещу дублиране
 *    - Неблокираща опашка от команди (премахнат блокиращият цикъл на изчакване)
 *    - Следене на прекъсната връзка с подчинения модул
 *    - Извеждане на причината за рестарт и броя грешки от датчика
 * ============================================================================
 */

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESP8266LLMNR.h>
#include <EEPROM.h>

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

#define FW_VERSION    2
#define PROTO_VERSION 2

// ===========================================================================
//  СТРУКТУРИ НА ОБМЕНА  (ЗАДЪЛЖИТЕЛНО ИДЕНТИЧНИ В ДВАТА ФАЙЛА!)
// ===========================================================================

// Телеметрия: подчинен -> главен модул
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
    uint16_t dhtErrors;        // брой неуспешни четения на DHT11
    uint16_t cfgInterval;      // потвърждение на приложената конфигурация
    float    cfgVccOffset;
    uint8_t  cfgChannel;
    uint8_t  fwVersion;
} struct_message;

// Типове команди: главен -> подчинен модул
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
    uint32_t seq;              // пореден номер срещу дублирани пакети
} struct_control;

struct_message incomingTelemetry;
struct_control outgoingControl;

// ===========================================================================
//  КОНФИГУРАЦИЯ В EEPROM
// ===========================================================================

#define CFG_MAGIC   0x484D4331UL   // "HMC1"
#define CFG_VERSION 2
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
    uint32_t cmdSeq;             // запазен пореден номер на командите
    uint32_t crc;
} Config;

Config cfg;

ESP8266WebServer server(80);

// --- Кеширани стойности от последния получен пакет ---
float    lastTemp = 0.0, lastHum = 0.0;
int      lastId = 0;
unsigned long lastDataTime = 0;
bool     currentRelayState[4] = {false, false, false, false};
bool     lastHr202State = true;
uint32_t lastFreeHeap = 0;
float    lastVcc = 0.0;
uint32_t lastLoopTime = 0, lastUptime = 0;
String   lastResetReason = "N/A";
uint32_t lastSuccessPackets = 0, lastFailedPackets = 0;
uint16_t lastDhtErrors = 0;
uint16_t lastCfgInterval = 0;
float    lastCfgVccOffset = 0.0;
uint8_t  lastCfgChannel = 0;
uint8_t  lastFwVersion = 0;
bool     everReceived = false;

// --- Състояние на изпращането ---
volatile bool waitingForDelivery = false;
volatile bool deliverySuccess = false;

String   lastErrorMsg = "";
unsigned long lastErrorTime = 0;

// ===========================================================================
//  НЕБЛОКИРАЩА ОПАШКА ОТ КОМАНДИ
//  Премахва блокиращия цикъл "while (waitingForDelivery)" от обработчиците.
// ===========================================================================
#define QUEUE_SIZE 12
#define ACK_TIMEOUT_MS 500
#define CMD_SPACING_MS 250

typedef struct { uint8_t relayIndex; bool state; } QueueItem;
QueueItem cmdQueue[QUEUE_SIZE];
uint8_t qHead = 0, qCount = 0;

enum QState { Q_IDLE, Q_WAIT_ACK, Q_COOLDOWN };
QState qState = Q_IDLE;
unsigned long qSendTime = 0, qCooldownStart = 0;

bool enqueueRelay(uint8_t idx, bool state) {
  if (qCount >= QUEUE_SIZE) { lastErrorMsg = "Опашката е препълнена"; lastErrorTime = millis(); return false; }
  uint8_t pos = (qHead + qCount) % QUEUE_SIZE;
  cmdQueue[pos].relayIndex = idx;
  cmdQueue[pos].state = state;
  qCount++;
  return true;
}

void sendControl(uint8_t type, uint8_t idx, bool state) {
  outgoingControl.protoVersion      = PROTO_VERSION;
  outgoingControl.cmdType           = type;
  outgoingControl.relayIndex        = idx;
  outgoingControl.relayState        = state;
  outgoingControl.telemetryInterval = cfg.telemetryInterval;
  outgoingControl.vccOffset         = cfg.vccOffset;
  outgoingControl.wifiChannel       = cfg.wifiChannel;
  outgoingControl.seq               = ++cfg.cmdSeq;
  waitingForDelivery = true;
  deliverySuccess = false;
  esp_now_send(cfg.peerMac, (uint8_t *)&outgoingControl, sizeof(outgoingControl));
}

void processQueue() {
  switch (qState) {
    case Q_IDLE:
      if (qCount > 0) {
        sendControl(CMD_RELAY, cmdQueue[qHead].relayIndex, cmdQueue[qHead].state);
        qSendTime = millis();
        qState = Q_WAIT_ACK;
      }
      break;

    case Q_WAIT_ACK:
      if (!waitingForDelivery) {                      // получено потвърждение
        if (deliverySuccess) {
          currentRelayState[cmdQueue[qHead].relayIndex] = cmdQueue[qHead].state;
        } else {
          lastErrorMsg = "Подчиненият модул не отговаря";
          lastErrorTime = millis();
        }
        qHead = (qHead + 1) % QUEUE_SIZE; qCount--;
        qCooldownStart = millis(); qState = Q_COOLDOWN;
      } else if (millis() - qSendTime > ACK_TIMEOUT_MS) {
        waitingForDelivery = false;
        lastErrorMsg = "Изтекло време за потвърждение";
        lastErrorTime = millis();
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
  cfg.vccOffset         = 0.30;
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
    .card.cfg-card { border-color: #607d8b; grid-column: 1 / -1; background: #16191c; }
    .diag-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin-top: 15px; }
    .diag-item { background: rgba(255,255,255,0.05); padding: 15px; border-radius: 8px; font-size: 1.1rem; }
    .diag-label { color: #aaa; font-size: 0.85rem; display: block; text-transform: uppercase; margin-bottom: 5px; }
    .diag-val { font-weight: bold; color: #00d2ff; word-break: break-all; }
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
    <div class="card relay-card"><div class="title">🔌 Реле 4 (D3)</div><button id="relayBtn3" class="action-on" onclick="toggleRelay(3)">ВКЛЮЧИ</button></div>

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
        <div class="diag-item"><span class="diag-label">Време за цикъл (CPU)</span><span class="diag-val" id="d-cpu">--</span> ms</div>
        <div class="diag-item"><span class="diag-label">Време на работа</span><span class="diag-val" id="d-upt">--</span> s</div>
        <div class="diag-item"><span class="diag-label">Причина за рестарт</span><span class="diag-val" id="d-rst">--</span></div>
        <div class="diag-item"><span class="diag-label">Грешки от DHT11</span><span class="diag-val" id="d-dht">--</span></div>
        <div class="diag-item"><span class="diag-label">Команди в опашка</span><span class="diag-val" id="d-que">--</span></div>
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

    function toggleRelay(idx) {
      const btn = document.getElementById('relayBtn' + idx);
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
    function updateButtonUI(idx, state) {
      const btn = document.getElementById('relayBtn' + idx);
      if (state) { btn.innerText = 'ИЗКЛЮЧИ'; btn.classList.remove('action-on'); btn.classList.add('action-off'); }
      else       { btn.innerText = 'ВКЛЮЧИ';  btn.classList.remove('action-off'); btn.classList.add('action-on'); }
    }

    function showBanner(text, kind) {
      const b = document.getElementById('banner');
      if (!text) { b.className = ''; b.style.display = 'none'; return; }
      b.innerText = text; b.className = kind;
    }

    fetch('/config').then(r=>r.json()).then(c => {
      document.getElementById('c-ssid').innerText = c.ssid;
      document.getElementById('c-name').innerText = c.name + '.local';
      document.getElementById('c-ip').innerText   = c.ip;
      document.getElementById('c-mac').innerText  = c.mac;
      document.getElementById('c-int').innerText  = c.interval;
      document.getElementById('c-off').innerText  = c.vccOffset.toFixed(2);
      document.getElementById('c-ch').innerText   = c.hubChannel + ' / ' + c.slaveChannel;
      document.getElementById('c-fw').innerText   = 'M' + c.fwMaster;
    });

    setInterval(function() {
      fetch('/data').then(response => response.json()).then(data => {
          document.getElementById('temp').innerText = data.temperature.toFixed(2);
          document.getElementById('hum').innerText  = data.humidity.toFixed(2);
          document.getElementById('pkt').innerText  = data.id;

          const hr202El = document.getElementById('hr202');
          if (data.hr202) { hr202El.innerText = "СУХО"; hr202El.style.color = "#8bc34a"; safetyLocked = false; }
          else { hr202El.innerText = "МОКРО (БЛОКИРАНО)"; hr202El.style.color = "#2196F3"; safetyLocked = true; }

          data.relays.forEach((state, idx) => updateButtonUI(idx, state));

          let totalPackets = data.success + data.failed;
          let health = totalPackets > 0 ? ((data.success / totalPackets) * 100).toFixed(1) : 100;
          document.getElementById('d-health').innerText = health;
          document.getElementById('d-fail').innerText = data.failed;
          document.getElementById('d-ram').innerText  = data.freeHeap;
          document.getElementById('d-vcc').innerText  = data.vcc.toFixed(2);
          document.getElementById('d-cpu').innerText  = data.loopTime;
          document.getElementById('d-upt').innerText  = data.uptime;
          document.getElementById('d-rst').innerText  = data.resetReason;
          document.getElementById('d-dht').innerText  = data.dhtErrors;
          document.getElementById('d-que').innerText  = data.queue;
          document.getElementById('c-fw').innerText   = 'M' + data.fwMaster + ' / S' + data.fwSlave;

          if (data.linkAlarm) {
            showBanner('⛔ НЯМА ВРЪЗКА С ПОДЧИНЕНИЯ МОДУЛ (' + data.timeSince + ' s без данни)', 'alarm');
          } else if (data.error && data.error.length > 0) {
            showBanner('⚠️ ' + data.error, 'err');
          } else {
            showBanner('', '');
          }

          document.getElementById('status').innerText = 'Онлайн (преди ' + data.timeSince + ' сек)';
          document.getElementById('status').style.color = data.linkAlarm ? '#f44336' : '#4CAF50';
        }).catch(error => {
          document.getElementById('status').innerText = 'Връзката е прекъсната';
          document.getElementById('status').style.color = '#f44336';
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
  Serial.println(F("[M] ТОВА МЕНЮ"));
  Serial.println(F("------------------------------------------"));
  Serial.println(F("Отделно реле:  R1 ON | R2 OFF | ... | R4 ON"));
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
  Serial.println(F("\n--- ТЕКУЩА КОНФИГУРАЦИЯ ---"));
  Serial.print(F("SSID              : ")); Serial.println(cfg.ssid);
  Serial.print(F("Парола            : ")); Serial.println(strlen(cfg.pass) ? F("(зададена)") : F("(празна)"));
  Serial.print(F("mDNS име          : ")); Serial.print(cfg.mdnsName); Serial.println(F(".local"));
  Serial.print(F("IP адрес          : ")); Serial.println(WiFi.localIP().toString());
  Serial.print(F("MAC на хъба       : ")); Serial.println(WiFi.macAddress());
  Serial.print(F("MAC на модула     : ")); Serial.println(macToString(cfg.peerMac));
  Serial.print(F("Канал на хъба     : ")); Serial.println(WiFi.channel());
  Serial.print(F("Канал на модула   : ")); Serial.println(cfg.wifiChannel);
  Serial.print(F("Интервал телем.   : ")); Serial.print(cfg.telemetryInterval); Serial.println(F(" ms"));
  Serial.print(F("Корекция VCC      : ")); Serial.print(cfg.vccOffset, 2); Serial.println(F(" V"));
  Serial.print(F("Парола за OTA     : ")); Serial.println(strlen(cfg.otaPass) ? F("(зададена)") : F("(без защита!)"));
  Serial.print(F("Уеб автентикация  : ")); Serial.println(cfg.webAuthEnabled ? F("ВКЛЮЧЕНА") : F("ИЗКЛЮЧЕНА"));
  Serial.print(F("Уеб потребител    : ")); Serial.println(cfg.webUser);
  Serial.print(F("Праг за авария    : ")); Serial.print(cfg.linkTimeout); Serial.println(F(" s"));
  Serial.print(F("Криптиране ESP-NOW: ")); Serial.println(USE_ESPNOW_ENCRYPTION ? F("ВКЛЮЧЕНО") : F("ИЗКЛЮЧЕНО"));
  Serial.print(F("Пореден № команда : ")); Serial.println(cfg.cmdSeq);
  Serial.print(F("Версия фърмуер    : ")); Serial.print(FW_VERSION);
  Serial.print(F(" / модул: ")); Serial.println(lastFwVersion);
  Serial.println(F("---------------------------\n"));
}

void printStatus() {
  Serial.println(F("\n--- ТЕКУЩ СТАТУС НА СИСТЕМАТА ---"));
  if (!everReceived) {
    Serial.println(F("⚠️ Все още няма получени данни от подчинения модул."));
  }
  Serial.print(F("Температура    : ")); Serial.print(lastTemp); Serial.println(F(" °C"));
  Serial.print(F("Влажност       : ")); Serial.print(lastHum);  Serial.println(F(" %"));
  Serial.print(F("HR202          : ")); Serial.println(lastHr202State ? F("СУХО") : F("МОКРО (блокирано)"));
  Serial.print(F("Релета         : "));
  for (int i = 0; i < 4; i++) {
    Serial.print(F("[R")); Serial.print(i + 1); Serial.print(F(": "));
    Serial.print(currentRelayState[i] ? F("ВКЛ") : F("ИЗКЛ")); Serial.print(F("] "));
  }
  Serial.println();
  Serial.print(F("Успешни пакети : ")); Serial.println(lastSuccessPackets);
  Serial.print(F("Загубени пакети: ")); Serial.println(lastFailedPackets);
  Serial.print(F("Грешки DHT11   : ")); Serial.println(lastDhtErrors);
  Serial.print(F("Причина рестарт: ")); Serial.println(lastResetReason);
  Serial.print(F("Свободна RAM   : ")); Serial.print(lastFreeHeap); Serial.println(F(" B"));
  Serial.print(F("VCC на модула  : ")); Serial.print(lastVcc, 2); Serial.println(F(" V"));
  Serial.print(F("Време на цикъла: ")); Serial.print(lastLoopTime); Serial.println(F(" ms"));
  Serial.print(F("Uptime на модул: ")); Serial.print(lastUptime); Serial.println(F(" s"));
  unsigned long since = everReceived ? (millis() - lastDataTime) / 1000 : 0;
  Serial.print(F("Последен пакет : преди ")); Serial.print(since); Serial.println(F(" s"));
  Serial.print(F("Команди в опашка: ")); Serial.println(qCount);
  Serial.println(F("---------------------------------\n"));
}

void pushConfigToSlave() {
  Serial.println(F("📤 Изпращам конфигурацията към подчинения модул..."));
  sendControl(CMD_CONFIG, 0, false);
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
        memcpy(cfg.peerMac, m, 6);
        Serial.print(F("✅ MAC на модула: ")); Serial.println(macToString(cfg.peerMac));
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

  // ---------- Оперативни команди ----------
  if (up == "M" || up == "MENU" || up == "HELP") { printUARTMenu(); return; }
  if (up == "6" || up == "CONFIG")               { printConfig();   return; }
  if (up == "1" || up == "STATUS")               { printStatus();   return; }

  if (up == "2" || up == "ALL ON") {
    if (!lastHr202State) { Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
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
    sendControl(CMD_RESTART, 0, false);
    return;
  }

  // R<n> ON / OFF
  if (up.startsWith("R") && up.length() >= 5) {
    int relayNum = up.substring(1, 2).toInt() - 1;
    String action = up.substring(3); action.trim();
    if (relayNum >= 0 && relayNum < 4 && (action == "ON" || action == "OFF")) {
      bool targetState = (action == "ON");
      if (targetState && !lastHr202State) { Serial.println(F("⚠️ Отчетена е влага - включването е отказано.")); return; }
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

// ===========================================================================
//  ESP-NOW CALLBACKS
// ===========================================================================

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  deliverySuccess = (sendStatus == 0);
  waitingForDelivery = false;
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len != sizeof(incomingTelemetry)) return;          // защита от чужд/стар пакет
  memcpy(&incomingTelemetry, incomingData, sizeof(incomingTelemetry));
  if (incomingTelemetry.protoVersion != PROTO_VERSION) return;

  lastTemp           = incomingTelemetry.temperature;
  lastHum            = incomingTelemetry.humidity;
  lastId             = incomingTelemetry.readingId;
  lastHr202State     = incomingTelemetry.hr202State;
  lastFreeHeap       = incomingTelemetry.freeHeap;
  lastVcc            = incomingTelemetry.vcc;
  lastLoopTime       = incomingTelemetry.loopTime;
  lastUptime         = incomingTelemetry.uptime;
  lastResetReason    = String(incomingTelemetry.resetReason);
  lastSuccessPackets = incomingTelemetry.successPackets;
  lastFailedPackets  = incomingTelemetry.failedPackets;
  lastDhtErrors      = incomingTelemetry.dhtErrors;
  lastCfgInterval    = incomingTelemetry.cfgInterval;
  lastCfgVccOffset   = incomingTelemetry.cfgVccOffset;
  lastCfgChannel     = incomingTelemetry.cfgChannel;
  lastFwVersion      = incomingTelemetry.fwVersion;
  lastDataTime       = millis();
  everReceived       = true;
  for (int i = 0; i < 4; i++) currentRelayState[i] = incomingTelemetry.relayState[i];
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
//  SETUP
// ===========================================================================

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);           // не блокира loop() при непълен ред
  Serial.println(F("\n\n--- СТАРТИРАНЕ НА ГЛАВНИЯ МОДУЛ ---"));

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

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
    int idx = server.arg("id").toInt();
    if (idx < 0 || idx > 3) { server.send(400, "application/json", "{\"error\":\"Невалиден индекс\"}"); return; }
    bool intended = !currentRelayState[idx];
    if (!lastHr202State && intended) {
      server.send(403, "application/json", "{\"error\":\"Отчетена е влага - включването е блокирано\"}");
      return;
    }
    if (!enqueueRelay(idx, intended)) {
      server.send(503, "application/json", "{\"error\":\"Опашката е препълнена\"}");
      return;
    }
    // Оптимистичен отговор; истинското състояние идва със следващата телеметрия.
    server.send(200, "application/json", String("{\"state\":") + (intended ? "true" : "false") + "}");
  });

  server.on("/onAll", []() {
    if (!ensureAuth()) return;
    if (!lastHr202State) { server.send(403, "application/json", "{\"error\":\"Отчетена е влага\"}"); return; }
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (!currentRelayState[i] && enqueueRelay(i, true)) n++;
    server.send(200, "application/json", String("{\"queued\":") + n + "}");
  });

  server.on("/offAll", []() {
    if (!ensureAuth()) return;
    uint8_t n = 0;
    for (int i = 0; i < 4; i++) if (enqueueRelay(i, false)) n++;
    server.send(200, "application/json", String("{\"queued\":") + n + "}");
  });

  server.on("/config", []() {
    if (!ensureAuth()) return;
    String j = "{";
    j += "\"ssid\":\""     + String(cfg.ssid) + "\",";
    j += "\"name\":\""     + String(cfg.mdnsName) + "\",";
    j += "\"ip\":\""       + WiFi.localIP().toString() + "\",";
    j += "\"mac\":\""      + macToString(cfg.peerMac) + "\",";
    j += "\"hubChannel\":" + String(WiFi.channel()) + ",";
    j += "\"slaveChannel\":" + String(cfg.wifiChannel) + ",";
    j += "\"interval\":"   + String(cfg.telemetryInterval) + ",";
    j += "\"vccOffset\":"  + String(cfg.vccOffset, 2) + ",";
    j += "\"fwMaster\":"   + String(FW_VERSION);
    j += "}";
    server.send(200, "application/json", j);
  });

  server.on("/data", []() {
    if (!ensureAuth()) return;
    unsigned long since = everReceived ? (millis() - lastDataTime) / 1000 : 0;
    bool linkAlarm = (!everReceived) || (since > cfg.linkTimeout);
    String err = (millis() - lastErrorTime < 8000) ? lastErrorMsg : "";

    String json = "{";
    json += "\"temperature\":" + String(lastTemp) + ",";
    json += "\"humidity\":"    + String(lastHum) + ",";
    json += "\"id\":"          + String(lastId) + ",";
    json += "\"timeSince\":"   + String(since) + ",";
    json += "\"hr202\":"       + String(lastHr202State ? "true" : "false") + ",";
    json += "\"freeHeap\":"    + String(lastFreeHeap) + ",";
    json += "\"vcc\":"         + String(lastVcc) + ",";
    json += "\"loopTime\":"    + String(lastLoopTime) + ",";
    json += "\"uptime\":"      + String(lastUptime) + ",";
    json += "\"resetReason\":\"" + lastResetReason + "\",";
    json += "\"dhtErrors\":"   + String(lastDhtErrors) + ",";
    json += "\"success\":"     + String(lastSuccessPackets) + ",";
    json += "\"failed\":"      + String(lastFailedPackets) + ",";
    json += "\"queue\":"       + String(qCount) + ",";
    json += "\"linkAlarm\":"   + String(linkAlarm ? "true" : "false") + ",";
    json += "\"error\":\""     + err + "\",";
    json += "\"fwMaster\":"    + String(FW_VERSION) + ",";
    json += "\"fwSlave\":"     + String(lastFwVersion) + ",";
    json += "\"relays\":["     + String(currentRelayState[0] ? "true" : "false") + ","
                               + String(currentRelayState[1] ? "true" : "false") + ","
                               + String(currentRelayState[2] ? "true" : "false") + ","
                               + String(currentRelayState[3] ? "true" : "false") + "]";
    json += "}";
    server.send(200, "application/json", json);
  });

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
}

// ===========================================================================
//  LOOP
// ===========================================================================

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  MDNS.update();
  handleSerialCommands();
  processQueue();          // неблокиращо изпълнение на командите
}

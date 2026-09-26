# ПРИЛОЖЕНИЕ Б

## Програмно осигуряване – структури на данните и фрагменти от програмите

Дадени са таблиците и фрагментите от програмите на двата възела (версия 3), към които се
препраща в Глава 3. Пълният изходен код на двете програми е приложен в електронен вид
(папка `firmware/v3`).

**Таблица Б.1. Конфигурационни параметри**

| Параметър | Възел | По подразбиране | Допустими стойности |
|---|---|---|---|
| Име на мрежата (SSID) / парола | Г | – | до 32 / до 64 знака |
| Име за mDNS | Г | `smarthome` | до 31 знака |
| MAC адрес на другия възел | Г, П | адресът в макета | `xx:xx:xx:xx:xx:xx` |
| Wi-Fi канал на подчинения възел | Г, П | 6 | 1…13 |
| Интервал на телеметрията | Г, П | 2000 ms | 500…60 000 ms |
| Корекция на напрежението | Г, П | 0 V | −2…+2 V |
| Парола за OTA | Г | `esp8266ota` | до 32 знака |
| Уеб потребител / парола; автентикация | Г | `admin` / `admin`; изкл. | до 16 / 32 знака; 0 или 1 |
| Праг за авария на връзката | Г | 10 s | 3…300 s |

*Г – главен възел; П – подчинен възел.*

**Таблица Б.2. Команди по UART**

| Команда | Действие | Възел |
|---|---|---|
| `SET SSID <име>`, `SET PASS <парола>`, `SET NAME <име>` | мрежа и име за mDNS | Г |
| `SET MAC`, `SET CHANNEL`, `SET INTERVAL`, `SET VCCOFFSET` | адрес на другия възел, канал, интервал, корекция | Г, П |
| `SET OTAPASS`, `SET WEBUSER`, `SET WEBPASS`, `SET WEBAUTH`, `SET LINKTIMEOUT` | защита на достъпа, праг за авария | Г |
| `SAVE`, `FACTORY` | запис в EEPROM; фабрични настройки и рестарт | Г, П |
| `PUSH` | изпраща интервала, корекцията и канала към подчинения възел | Г |
| `1`…`7`, `M`, `R<n> ON`, `R<n> OFF` | статус, релета, рестарт, конфигурация, измервания, меню | Г, П |
| `PING <n>`, `LOG ON`, `LOG OFF`, `RESET STATS` | тест на закъснението, запис във формат CSV, нулиране на броячите | Г (`RESET STATS` – и П) |

**Таблица Б.3. Състав на отчета на подчинения възел (144 B)**

| Група | Съдържание | Размер |
|---|---|---|
| Заглавие | версии на протокола и програмата, вид на отчета, канал, пореден номер, номер на последната изпълнена команда | 12 B |
| Датчици и изходи | DHT11: температура, влажност, валидност, брой грешки; HR202: сухо/мокро; блокировка; 4 релета | 17 B |
| Напрежение и конфигурация | сума на пробите от A0, напрежение, интервал, корекция | 12 B |
| Състояние на системата | време на работа, причина за последния рестарт | 36 B |
| Връзка и команди | потвърдени и непотвърдени изпращания, отхвърлени команди | 10 B |
| Време за цикъл | брой итерации, средно и най-голямо време за изпълнение, най-голям период, прозорец, максимум от стартирането | 24 B |
| Памет, бутони, защита | свободна памет и фрагментация; фронтове и натискания на бутоните; задействания и откази на защитата | 31 B |
| Подравняване | – | 2 B |

**Таблица Б.4. Адреси на уеб сървъра**

| Адрес | Параметри | Действие | Отговор / кодове за грешка |
|---|---|---|---|
| `/` | – | уеб страницата | HTML |
| `/data` | – | състояние, измервания, диагностика | JSON, ≈ 1 kB |
| `/config` | – | конфигурация, без паролите | JSON |
| `/toggle` | `id` = 0…3 | превключва реле | 400 – индекс; 403 – влага; 503 – пълна опашка |
| `/onAll`, `/offAll` | – | включва или изключва всички релета | 403 – влага |
| `/ping` | `n` = 1…1000 | стартира теста на закъснението | 409 – тестът се изпълнява |
| `/resetStats` | – | нулира броячите в двата възела | – |


**Листинг Б.1. Разбор на командите `SET <ключ> <стойност>` в главния възел (фрагмент)**

```cpp
  // up - редът в главни букви, raw - оригиналът
  if (up.startsWith("SET ")) {
    int sp = raw.indexOf(' ', 4);           // интервалът след ключа
    String key = (sp < 0 ? raw.substring(4) : raw.substring(4, sp));
    String val = (sp < 0 ? String("") : raw.substring(sp + 1));
    key.trim(); key.toUpperCase(); val.trim();   // стойността запазва регистъра
    if (val.length() == 0) { Serial.println(F("Липсва стойност.")); return; }

    if (key == "SSID") {
      strncpy(cfg.ssid, val.c_str(), sizeof(cfg.ssid) - 1);
      cfg.ssid[sizeof(cfg.ssid) - 1] = 0;
    }
    else if (key == "CHANNEL") {
      int c = val.toInt();
      if (c >= 1 && c <= 13) cfg.wifiChannel = (uint8_t)c;
      else Serial.println(F("Допустим диапазон: 1-13."));
    }
    // ... останалите параметри по същия начин
    return;
  }
```

**Листинг Б.2. Приемане на команда в подчинения възел (фрагмент)**

```cpp
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (!isBroadcastMac(cfg.peerMac) && memcmp(mac, cfg.peerMac, 6) != 0) {
    foreignMacSeen = true; cmdRejected++; return;           // не е от главния възел
  }
  if (len >= 1 && incomingData[0] != PROTO_VERSION) {  // друга версия на протокола
    badProtoSeen = incomingData[0]; cmdRejected++; return;
  }
  if (len != sizeof(incomingControl)) { cmdRejected++; return; }
  memcpy(&incomingControl, incomingData, sizeof(incomingControl));
  if (haveLastSeq && incomingControl.seq == lastSeq) {     // повторно приет пакет
    cmdRejected++; return;
  }
  lastSeq = incomingControl.seq;
  haveLastSeq = true;
  // ... само запомняне на полетата - командата се изпълнява в loop()
  commandPending = true;
}
```

**Листинг Б.3. Форматиране на отговорите в статичния буфер на главния възел**

```cpp
__attribute__((format(printf, 1, 2)))     // компилаторът проверява аргументите
static void jsonPrintf(const char *fmt, ...) {
  if (jsonOverflow) return;
  size_t room = JSON_BUF_SIZE - jsonLen;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(jsonBuf + jsonLen, room, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= room) {
    jsonOverflow = true; jsonBuf[jsonLen] = '\0'; return;
  }
  jsonLen += (size_t)n;
}
```

**Листинг Б.4. Филтър срещу трептене и броене на фронтовете в подчинения възел (бутон SB1)**

```cpp
void IRAM_ATTR isrButtonToggle() { btnEdges[0]++; }   // прекъсване по всеки фронт

void handleButtons() {
  unsigned long now = millis();
  bool r = digitalRead(BUTTON_TOGGLE_PIN);
  if (r != toggleLastRead) { toggleLastRead = r; toggleLastChange = now; }
  else if (now - toggleLastChange > DEBOUNCE_MS && r != toggleStable) {   // 60 ms
    toggleStable = r;
    noteButtonTransition(0);                 // фронтове от предходното приемане
    if (toggleStable == LOW) {               // натискане
      btnPresses[0]++;
      applyRelay(0, !currentRelayState[0]);  // при блокировка включването се отказва
      requestReport(REPORT_EVENT);
    }
  }
  // ... бутон SB2 (D6) - аналогично, изключва всички релета
}
```

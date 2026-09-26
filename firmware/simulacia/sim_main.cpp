// ============================================================================
//  Симулация на системата: двата непроменени фърмуера (Master.ino и Slave.ino)
//  работят едновременно в симулирано време и обменят кадри по модел на
//  радиоканала. Резултатът са величини, получени от самите програми, и
//  проверки на инвариантите (защита, съгласуваност на състоянието, отчитане
//  на загубите).
//
//  Модел на канала (IEEE 802.11b, 1 Mbit/s): DIFS 50 µs, слот 20 µs,
//  CW 31...1023 с удвояване при повторение, SIFS 10 µs, PLCP 192 µs,
//  ACK 14 B, до 7 опита на MAC ниво; независими грешки на кадрите с
//  вероятност FER; повторно доставяне на приложно ниво с вероятност DUP;
//  фонов трафик от други станции.
//
//  Употреба: sim <сценарий> <изходна папка>   (виж run_sim.sh)
// ============================================================================
#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <vector>

// ------------------------------------------------------------------ възли ---
struct Node {
  std::string name, so;           // име и път до библиотеката
  void *h = nullptr;
  int boots = 0;
  int64_t offset = 0;             // глобално време = локално + offset (µs)
  uint8_t mac[6];
  uint8_t eeprom[4096];
  bool haveEeprom = false;
  std::string serialBuf;          // непрочетен изход по UART
  FILE *log = nullptr;

  int (*setup)();
  int (*loop)();
  uint64_t (*now)();
  void (*set_now)(uint64_t);
  void (*set_pin)(int, int);
  int (*get_pin)(int);
  void (*set_random)(uint32_t);
  void (*dht)(int);
  int (*take_sent)(uint8_t *, uint8_t *, int, uint64_t *);
  void (*deliver)(const uint8_t *, const uint8_t *, int);
  void (*status)(const uint8_t *, int);
  void (*serial_in)(const char *);
  size_t (*serial_take)(char *, size_t);
  void (*eeprom_get)(uint8_t *);
  void (*eeprom_set)(const uint8_t *);
  int (*http)(const char *, const char *, const char *, char *, int) = nullptr;
  int (*locked)() = nullptr;
  double (*get)(const char *);

  int64_t gnow() { return (int64_t)now() + offset; }
};

template <typename F> static void sym(Node &n, F &f, const char *s, bool must = true) {
  f = (F)dlsym(n.h, s);
  if (!f && must) { fprintf(stderr, "липсва %s в %s\n", s, n.so.c_str()); exit(2); }
}

// „Включване на захранването“: зарежда ново копие на библиотеката
static void boot(Node &n, int64_t gt, uint32_t rnd, const std::string &tmpdir) {
  if (n.h) { n.eeprom_get(n.eeprom); n.haveEeprom = true; dlclose(n.h); n.h = nullptr; }
  std::string copy = tmpdir + "/" + n.name + "_" + std::to_string(n.boots) + ".so";
  std::string cmd = "cp '" + n.so + "' '" + copy + "'";
  if (system(cmd.c_str()) != 0) { fprintf(stderr, "cp\n"); exit(2); }
  n.h = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!n.h) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(2); }
  unlink(copy.c_str());
  sym(n, n.setup, "sim_setup"); sym(n, n.loop, "sim_loop"); sym(n, n.now, "sim_now");
  sym(n, n.set_now, "sim_set_now"); sym(n, n.set_pin, "sim_set_pin"); sym(n, n.get_pin, "sim_get_pin");
  sym(n, n.set_random, "sim_set_random"); sym(n, n.dht, "sim_dht"); sym(n, n.take_sent, "sim_take_sent");
  sym(n, n.deliver, "sim_deliver"); sym(n, n.status, "sim_status"); sym(n, n.serial_in, "sim_serial_in");
  sym(n, n.serial_take, "sim_serial_take"); sym(n, n.eeprom_get, "sim_eeprom_get");
  sym(n, n.eeprom_set, "sim_eeprom_set"); sym(n, n.get, "sim_get"); sym(n, n.http, "sim_http", false);
  sym(n, n.locked, "sim_locked", false);
  n.boots++;
  n.set_now(1000000ULL);                       // скицата започва от 1 s след подаване на захранване
  n.offset = gt - 1000000LL;
  if (n.haveEeprom) n.eeprom_set(n.eeprom);
  n.set_random(rnd);
  if (n.setup() != 0) { fprintf(stderr, "%s: рестарт в setup()\n", n.name.c_str()); exit(2); }
}

// --------------------------------------------------------------- сценарий ---
struct Scenario {
  std::string name;
  double hours = 1;
  uint32_t gap = 20;              // µs между итерациите (работа на SDK)
  double fer = 0;                 // вероятност за грешка на кадъра при един опит
  double ackFer = -1;             // ... на кадъра за потвърждение (-1 = като fer)
  double dup = 0;                 // повторно доставяне на приложно ниво
  double bg = 0;                  // дял от времето, в който каналът е зает от други станции
  int attempts = 7;               // опити на MAC ниво
  double toggleS = 0;             // средно време между команди от браузъра, s
  bool browser = true;            // /data всяка секунда
  double pressS = 0;              // средно време между натискания на бутон, s
  double shortTapShare = 0;       // дял на много кратките почуквания
  double wetS = 0;                // средно време между намокряния, s
  bool chatter = true;            // трептене на изхода на компаратора при намокряне и изсъхване
  double dhtFail = 0;             // вероятност за грешка на DHT11 при четене
  bool ping = false;              // непрекъснат тест PING
  std::vector<std::pair<double, double>> outages;       // [от, до], s
  std::vector<std::pair<double, std::string>> events;   // момент, s -> събитие
  uint64_t seed = 1;
};

static Scenario scenario(const std::string &s) {
  Scenario c; c.name = s;
  if (s == "rtt") {                      // закъснение при идеален канал
    c.hours = 1; c.gap = 20; c.ping = true; c.browser = false; c.seed = 11;
  } else if (s == "rtt_bg") {            // закъснение при зает канал и грешки
    c.hours = 1; c.gap = 20; c.ping = true; c.browser = true; c.fer = 0.2; c.bg = 0.3; c.seed = 12;
  } else if (s.rfind("fer", 0) == 0) {   // зашумен канал: fer10, fer30, ...
    c.hours = 2; c.gap = 50; c.fer = atof(s.c_str() + 3) / 100.0; c.toggleS = 10; c.browser = true;
    c.dup = 0.002; c.seed = 100 + (uint64_t)atof(s.c_str() + 3);
  } else if (s == "buttons") {
    c.hours = 2; c.gap = 50; c.pressS = 0.6; c.shortTapShare = 0.15; c.browser = true; c.seed = 21;
  } else if (s == "wet") {
    c.hours = 3; c.gap = 50; c.wetS = 20; c.browser = true; c.seed = 31;
  } else if (s == "wet_clean") {         // намокряне без трептене - чисто време за реакция
    c.hours = 3; c.gap = 50; c.wetS = 20; c.browser = true; c.chatter = false; c.seed = 32;
  } else if (s == "long") {              // 24 h с всички въздействия
    c.hours = 24; c.gap = 200; c.fer = 0.1; c.dup = 0.001; c.bg = 0.1; c.toggleS = 60;
    c.pressS = 180; c.shortTapShare = 0.1; c.wetS = 3600; c.dhtFail = 0.01; c.seed = 41;
    c.outages = {{3 * 3600.0, 3 * 3600.0 + 30}, {20 * 3600.0, 20 * 3600.0 + 90}};
    c.events = {{6 * 3600.0, "slave_power"}, {12 * 3600.0, "slave_cmd_restart"},
                {18 * 3600.0, "master_restart"}, {9 * 3600.0, "burst"}};
  } else {
    fprintf(stderr, "непознат сценарий %s\n", s.c_str()); exit(2);
  }
  return c;
}

// ---------------------------------------------------------- модел на канала --
static const int PLCP = 192, DIFS = 50, SIFS = 10, SLOT = 20, CWMIN = 31, CWMAX = 1023;
static int frameUs(int payload) { return PLCP + 8 * (payload + 43); }     // 1 Mbit/s
static const int ACK_US = PLCP + 8 * 14;
static const int ACK_TIMEOUT = SIFS + ACK_US + SLOT;

struct Ev {
  int64_t t = 0; int node = 0; int kind = 0;  // kind: 0 - доставяне, 1 - потвърждение
  std::vector<uint8_t> data; uint8_t mac[6] = {}; int st = 0;
  bool operator>(const Ev &o) const { return t > o.t; }
};

struct Stats {
  // канал
  long framesS = 0, framesM = 0, reportsSent = 0, reportsLostApp = 0, reportsDupApp = 0;
  long cmdsSent = 0, cmdsLostApp = 0, cmdsDupApp = 0, attemptsTotal = 0, ackLostDelivered = 0;
  long foreign = 0;
  // периодични отчети
  std::vector<double> periodMs;
  // съгласуваност на релетата (проби през 100 ms извън прекъсванията)
  long consSamples = 0, consBad = 0; double consMaxMs = 0, consCurMs = 0;
  // защита
  long wetEvents = 0, wetWithRelayOn = 0, lockViol = 0, onAttemptsLocked = 0, http403 = 0, lockPresses = 0;
  long consSamplesAll = 0, consBadAll = 0;
  std::vector<double> reactUs;
  // бутони
  long pressesLong = 0, pressesShort = 0;
  // уеб
  long toggles = 0, http503 = 0, httpOther = 0;
  // връзка
  long alarmSamples = 0; double alarmRaiseS = -1, alarmClearS = -1;
  // PING
  std::vector<double> rtt;
  // рестарти
  long slaveBoots = 0, masterBoots = 0;
  long queueStuck = 0;
  // броячи от предишните стартирания (RAM се нулира при рестарт)
  std::map<std::string, double> accS, accM;
  std::map<int, long> lockWebCodes;
  std::vector<std::pair<double, double>> mismatch;   // [начало, s; продължителност, ms]
};

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "sim <сценарий> <папка>\n"); return 2; }
  Scenario sc = scenario(argv[1]);
  std::string out = argv[2];
  std::string so = argc > 3 ? argv[3] : ".";
  if (argc > 4) sc.hours = atof(argv[4]);           // по-кратък опит за проверка
  std::mt19937_64 rng(sc.seed);
  auto U = [&]() { return std::uniform_real_distribution<double>(0, 1)(rng); };
  auto expo = [&](double m) { return -m * std::log(1 - U()); };

  Node S, M;
  S.name = "slave"; S.so = so + "/slave.so";
  M.name = "master"; M.so = so + "/master.so";
  const uint8_t MAC_S[6] = {0xBC, 0xFF, 0x4D, 0x1D, 0xA5, 0xA6};   // адресите по подразбиране в двете програми
  const uint8_t MAC_M[6] = {0xA4, 0xE5, 0x7C, 0x01, 0xE6, 0x9E};
  memcpy(S.mac, MAC_S, 6); memcpy(M.mac, MAC_M, 6);
  S.log = fopen((out + "/" + sc.name + "_slave.log").c_str(), "w");
  M.log = fopen((out + "/" + sc.name + "_master.log").c_str(), "w");

  const int64_t T0 = 1000000;
  const int64_t TEND = T0 + (int64_t)(sc.hours * 3600e6);
  boot(S, T0, 0x1111, out);
  boot(M, T0 + 3000, (uint32_t)rng(), out);
  Stats st;
  st.slaveBoots = 1; st.masterBoots = 1;
  const char *keysS[] = {"trips", "refusals", "presses0", "presses1", "rejected", "dhtErrors"};
  const char *keysM[] = {"rx", "lost", "dup", "bad", "reboots", "ackCount", "ackFail"};
  auto accumulate = [&](int idx) {
    if (idx == 0) for (auto k : keysS) st.accS[k] += S.get(k);
    else for (auto k : keysM) st.accM[k] += M.get(k);
  };
  M.serial_in("LOG ON");

  std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> evq[2];
  int64_t mediumFree = 0;
  int64_t radioFree[2] = {0, 0};
  int64_t lastPeriodicSend = -1;

  auto inOutage = [&](int64_t gt) {
    double s = (gt - T0) / 1e6;
    for (auto &o : sc.outages) if (s >= o.first && s < o.second) return true;
    return false;
  };

  // Предаване на кадър с повторения на MAC ниво
  auto transmit = [&](int from, const uint8_t *data, int len, int64_t tSend) {
    Node &src = from == 0 ? S : M;
    int to = 1 - from;
    int64_t t = std::max(tSend, radioFree[from]);
    int cw = CWMIN;
    bool delivered = false, acked = false;
    int64_t tDeliver = 0, tStatus = 0;
    double ackF = sc.ackFer < 0 ? sc.fer : sc.ackFer;
    for (int a = 0; a < sc.attempts; a++) {
      st.attemptsTotal++;
      // фонов трафик: със зададена вероятност каналът е зает от чужд кадър
      if (sc.bg > 0 && U() < sc.bg) t += (int64_t)(200 + U() * 1800);
      int64_t start = std::max(t, mediumFree) + DIFS + (int64_t)(std::uniform_int_distribution<int>(0, cw)(rng)) * SLOT;
      int64_t end = start + frameUs(len);
      bool lost = inOutage(start) || U() < sc.fer;
      bool ackLost = lost || inOutage(end) || U() < ackF;
      mediumFree = end + (lost ? 0 : SIFS + ACK_US);
      if (!lost && !delivered) { delivered = true; tDeliver = end; }
      if (!ackLost) { acked = true; tStatus = end + SIFS + ACK_US; break; }
      t = end + ACK_TIMEOUT;
      cw = std::min(2 * cw + 1, CWMAX);
      tStatus = t;
    }
    radioFree[from] = tStatus;
    // потвърждение към изпращача
    Ev es; es.t = tStatus; es.node = from; es.kind = 1; memcpy(es.mac, (from == 0 ? M.mac : S.mac), 6);
    es.st = acked ? 0 : 1;
    evq[from].push(es);
    if (delivered && !acked) st.ackLostDelivered++;
    bool isReport = from == 0;
    if (isReport) { st.framesS++; if (data[0] == 3) st.reportsSent++; }
    else { st.framesM++; st.cmdsSent++; }
    if (!delivered) { if (isReport) st.reportsLostApp++; else st.cmdsLostApp++; return; }
    Ev ed; ed.t = tDeliver; ed.node = to; ed.kind = 0; ed.data.assign(data, data + len);
    memcpy(ed.mac, src.mac, 6);
    evq[to].push(ed);
    if (sc.dup > 0 && U() < sc.dup) {                  // повторно доставяне
      Ev e2 = ed; e2.t += 1500 + (int64_t)(U() * 3000); evq[to].push(e2);
      if (isReport) st.reportsDupApp++; else st.cmdsDupApp++;
    }
  };

  // ------------------------------------------------ действия на „потребителите“
  struct Act { int64_t t; std::function<void()> f; bool operator>(const Act &o) const { return t > o.t; } };
  std::priority_queue<Act, std::vector<Act>, std::greater<Act>> acts;
  auto at = [&](int64_t t, std::function<void()> f) { acts.push({t, f}); };

  char body[4096];
  auto http = [&](const char *p, const char *k = "", const char *v = "") {
    return M.http(p, k, v, body, sizeof(body));
  };
  auto jsonBool = [&](const char *key) {
    std::string b(body), k = std::string("\"") + key + "\":";
    size_t i = b.find(k); return i != std::string::npos && b.compare(i + k.size(), 4, "true") == 0;
  };

  const int D5 = 14, D6 = 12, D7 = 13;
  const int RELAY[4] = {16, 5, 4, 15};

  std::function<void()> poll, tog, press, wet, pg, df;
  // браузър: /data всяка секунда
  bool prevAlarm = false;
  if (sc.browser) {
    poll = [&]() {
      http("/data");
      bool alarm = jsonBool("linkAlarm");
      int64_t g = M.gnow();
      st.alarmSamples++;
      if (alarm && !prevAlarm && st.alarmRaiseS < 0 && !sc.outages.empty()) {
        double s = (g - T0) / 1e6 - sc.outages[0].first;
        if (s > 0 && s < 200) st.alarmRaiseS = s;
      }
      if (!alarm && prevAlarm && st.alarmClearS < 0 && !sc.outages.empty()) {
        double s = (g - T0) / 1e6 - sc.outages[0].second;
        if (s > -1 && s < 200) st.alarmClearS = s;
      }
      prevAlarm = alarm;
      at(g + 1000000, poll);
    };
    at(T0 + 5000000, poll);
  }
  // команди от браузъра
  if (sc.toggleS > 0) {
    tog = [&]() {
      double r = U(); int code;
      if (r < 0.1) code = http("/onAll");
      else if (r < 0.2) code = http("/offAll");
      else { char id[4]; snprintf(id, sizeof(id), "%d", (int)(U() * 4)); code = http("/toggle", "id", id); }
      st.toggles++;
      if (code == 503) st.http503++; else if (code == 403) st.http403++; else if (code != 200) st.httpOther++;
      at(M.gnow() + (int64_t)(expo(sc.toggleS) * 1e6), tog);
    };
    at(T0 + 8000000, tog);
  }
  // бутони с трептене на контактите
  auto bounce = [&](int64_t t, int pin, int finalLevel) {
    int k = 1 + (int)(U() * 6);                    // 1...6 отскока
    int64_t tt = t;
    for (int i = 0; i < 2 * k - 1; i++) {
      int lvl = (i % 2 == 0) ? finalLevel : 1 - finalLevel;
      at(tt, [&, pin, lvl]() { S.set_pin(pin, lvl); });
      tt += 100 + (int64_t)(U() * 1500);           // 0,1...1,6 ms между фронтовете
    }
    return tt;
  };
  if (sc.pressS > 0) {
    press = [&]() {
      int64_t g = S.gnow();
      int pin = U() < 0.8 ? D5 : D6;
      bool shortTap = U() < sc.shortTapShare;
      int64_t hold = shortTap ? (int64_t)(20000 + U() * 25000) : (int64_t)(90000 + U() * 300000);
      int64_t t1 = bounce(g, pin, 0);
      bounce(t1 + hold, pin, 1);
      if (shortTap) st.pressesShort++; else st.pressesLong++;
      at(g + hold + 500000 + (int64_t)(expo(sc.pressS) * 1e6), press);
    };
    at(T0 + 10000000, press);
  }
  // намокряне на датчика (с трептене на изхода на компаратора)
  int64_t wetStart = -1; bool reacted = true;
  if (sc.wetS > 0) {
    wet = [&]() {
      http("/onAll");                                   // релетата се включват преди намокрянето
      int64_t g = M.gnow() + 3000000 + (int64_t)(U() * 1e6);
      int n = sc.chatter ? 2 + (int)(U() * 8) : 0;
      int64_t tt = g;
      at(tt, [&, tt]() {
        bool on = false;
        for (int i = 0; i < 4; i++) on |= S.get_pin(RELAY[i]) == 1;
        wetStart = tt; reacted = !on; st.wetEvents++; if (on) st.wetWithRelayOn++;
      });
      for (int i = 0; i < n; i++) {                     // трептене при намокряне
        at(tt, [&]() { S.set_pin(D7, 0); });
        tt += 2000 + (int64_t)(U() * 30000);
        at(tt, [&]() { S.set_pin(D7, 1); });
        tt += 2000 + (int64_t)(U() * 30000);
      }
      at(tt, [&]() { S.set_pin(D7, 0); });
      int64_t wetDur = (int64_t)((5 + U() * 25) * 1e6);
      // опити за включване по време на блокировката
      for (int i = 0; i < 3; i++) {
        int64_t ta = tt + 1000000 + (int64_t)(U() * (wetDur - 2000000));
        int kind = i;
        at(ta, [&, kind]() {
          st.onAttemptsLocked++;
          if (kind == 0) { int c = http("/toggle", "id", "0"); if (c == 403) st.http403++; st.lockWebCodes[c]++; }
          else if (kind == 1) M.serial_in("R2 ON");
          else { int64_t t1 = bounce(S.gnow(), D5, 0); bounce(t1 + 150000, D5, 1); st.lockPresses++; }
        });
      }
      tt += wetDur;
      int n2 = sc.chatter ? 2 + (int)(U() * 8) : 0;     // трептене при изсъхване
      for (int i = 0; i < n2; i++) {
        at(tt, [&]() { S.set_pin(D7, 1); });
        tt += 5000 + (int64_t)(U() * 200000);
        at(tt, [&]() { S.set_pin(D7, 0); });
        tt += 5000 + (int64_t)(U() * 200000);
      }
      at(tt, [&]() { S.set_pin(D7, 1); });
      at(tt + (int64_t)(expo(sc.wetS) * 1e6) + 5000000, wet);
    };
    at(T0 + 15000000, wet);
  }
  // тест PING
  if (sc.ping) {
    pg = [&]() {
      if (M.get("pingRunning") < 0.5) http("/ping", "n", "1000");
      at(M.gnow() + 2000000, pg);
    };
    at(T0 + 6000000, pg);
  }
  // грешки на DHT11
  if (sc.dhtFail > 0) {
    df = [&]() { S.dht(U() < sc.dhtFail); at(S.gnow() + 1000000, df); };
    at(T0 + 1000000, df);
  }
  // събития: рестарти, поредица от команди
  bool pendingSlavePower = false;
  for (auto &e : sc.events) {
    int64_t t = T0 + (int64_t)(e.first * 1e6);
    std::string kind = e.second;
    if (kind == "slave_power") at(t, [&]() { pendingSlavePower = true; });
    else if (kind == "slave_cmd_restart") at(t, [&]() { M.serial_in("5"); });
    else if (kind == "master_restart") at(t, [&]() { M.serial_in("4"); });
    else if (kind == "burst") at(t, [&]() {
      for (int i = 0; i < 30; i++) {
        char id[4]; snprintf(id, sizeof(id), "%d", i % 4);
        int c = http("/toggle", "id", id);
        if (c == 503) st.http503++;
      }
    });
  }

  // ------------------------------------------------------ основен цикъл ------
  char sbuf[1 << 16];
  auto drainSerial = [&](Node &n) {
    size_t k;
    while ((k = n.serial_take(sbuf, sizeof(sbuf))) > 0) {
      fwrite(sbuf, 1, k, n.log);
      if (&n == &M) {
        n.serialBuf.append(sbuf, k);
        size_t p;
        while ((p = n.serialBuf.find('\n')) != std::string::npos) {
          std::string line = n.serialBuf.substr(0, p);
          n.serialBuf.erase(0, p + 1);
          if (line.rfind("PING,", 0) == 0) {
            size_t c = line.rfind(',');
            std::string v = line.substr(c + 1);
            if (v != "LOST") st.rtt.push_back(atof(v.c_str()));
          }
        }
      }
    }
  };

  uint8_t mac[6], buf[256];
  uint64_t ts;
  long iter = 0;
  int64_t nextSample = T0 + 10000000;
  int64_t lastBoot = T0;
  int64_t qNotIdleSince = -1;
  double lastSeqSeen = -1;
  int64_t mismatchStart = -1;
  while (true) {
    int64_t gs = S.gnow(), gm = M.gnow();
    Node &n = gs <= gm ? S : M;
    int idx = gs <= gm ? 0 : 1;
    int64_t g = n.gnow();
    if (g >= TEND) break;

    // събития от канала за този възел (обратните функции - между итерациите)
    while (!evq[idx].empty() && evq[idx].top().t <= g) {
      Ev e = evq[idx].top(); evq[idx].pop();
      if (e.kind == 0) n.deliver(e.mac, e.data.data(), (int)e.data.size());
      else n.status(e.mac, e.st);
    }
    // действия на потребителите, чийто момент е настъпил
    while (!acts.empty() && acts.top().t <= g) { Act a = acts.top(); acts.pop(); a.f(); }
    if (pendingSlavePower && idx == 0) {
      pendingSlavePower = false;
      accumulate(0);
      boot(S, g, 0x2222 + st.slaveBoots, out); st.slaveBoots++;
      lastPeriodicSend = -1; lastBoot = g;
      continue;
    }

    int rc = n.loop();
    if (rc == 1) {                                   // рестарт, поискан от програмата
      accumulate(idx);
      boot(n, n.gnow(), idx == 0 ? 0x3333 + st.slaveBoots : (uint32_t)rng(), out);
      if (idx == 0) { st.slaveBoots++; lastPeriodicSend = -1; } else { st.masterBoots++; M.serial_in("LOG ON"); }
      lastBoot = n.gnow();
      continue;
    }
    // изпратени кадри
    int len;
    while ((len = n.take_sent(mac, buf, sizeof(buf), &ts)) >= 0) {
      int64_t tg = (int64_t)ts + n.offset;
      if (idx == 0 && len >= 3 && buf[0] == 3 && buf[2] == 0) {     // периодичен отчет
        if (lastPeriodicSend > 0) st.periodMs.push_back((tg - lastPeriodicSend) / 1000.0);
        lastPeriodicSend = tg;
      }
      transmit(idx, buf, len, tg);
    }
    // проверки след итерацията на подчинения възел
    if (idx == 0) {
      bool anyOn = false;
      for (int i = 0; i < 4; i++) anyOn |= S.get_pin(RELAY[i]) == 1;
      if (anyOn && S.locked()) st.lockViol++;
      if (!reacted && wetStart >= 0 && S.gnow() >= wetStart && !anyOn) {
        st.reactUs.push_back((double)(S.gnow() - wetStart)); reacted = true;
      }
    }
    // съгласуваност на релетата: проби през 100 ms
    int64_t gg = std::min(S.gnow(), M.gnow());
    if (gg >= nextSample) {
      nextSample += 100000;
      bool out_ = inOutage(gg) || gg - lastBoot < 5000000;   // прекъсване или до 5 s след рестарт
      bool okState = true;
      for (int i = 0; i < 4; i++) okState &= (S.get_pin(RELAY[i]) == 1) == (M.get(("rs" + std::to_string(i)).c_str()) > 0.5);
      st.consSamplesAll++; if (!okState) st.consBadAll++;
      if (!out_) {
        st.consSamples++;
        if (!okState) {
          st.consBad++; st.consCurMs += 100; st.consMaxMs = std::max(st.consMaxMs, st.consCurMs);
          if (mismatchStart < 0) mismatchStart = gg;
        } else {
          if (mismatchStart >= 0 && st.consCurMs >= 300)
            st.mismatch.push_back({(mismatchStart - T0) / 1e6, st.consCurMs});
          st.consCurMs = 0; mismatchStart = -1;
        }
      } else { st.consCurMs = 0; mismatchStart = -1; }
      // опашката не бива да остава заета
      double seqNow = M.get("cmdSeq");
      if ((M.get("qState") > 0.5 || M.get("qCount") > 0.5) && seqNow == lastSeqSeen) {
        if (qNotIdleSince < 0) qNotIdleSince = gg;
        else if (gg - qNotIdleSince > 30000000) { st.queueStuck++; qNotIdleSince = gg; }
      } else qNotIdleSince = -1;
      lastSeqSeen = seqNow;
    }
    n.set_now(n.now() + sc.gap);
    if (++iter % 512 == 0) { drainSerial(S); drainSerial(M); }
  }
  drainSerial(S); drainSerial(M);

  // ------------------------------------------------------------- резултати --
  http("/data");
  std::string data(body);
  FILE *f = fopen((out + "/" + sc.name + ".json").c_str(), "w");
  auto num = [&](const char *k, double v) { fprintf(f, "  \"%s\": %.6g,\n", k, v); };
  fprintf(f, "{\n  \"scenario\": \"%s\",\n", sc.name.c_str());
  num("hours", sc.hours); num("gap_us", sc.gap); num("fer", sc.fer); num("dup", sc.dup); num("bg", sc.bg);
  num("iterations", iter);
  num("slave_boots", st.slaveBoots); num("master_boots", st.masterBoots);
  num("reports_sent", st.reportsSent); num("reports_lost_channel", st.reportsLostApp);
  num("reports_dup_channel", st.reportsDupApp); num("cmds_sent", st.cmdsSent);
  num("cmds_lost_channel", st.cmdsLostApp); num("cmds_dup_channel", st.cmdsDupApp);
  num("attempts_total", st.attemptsTotal); num("ack_lost_but_delivered", st.ackLostDelivered);
  for (auto k : keysS) fprintf(f, "  \"total_slave_%s\": %.6g,\n", k, st.accS[k] + S.get(k));
  for (auto k : keysM) fprintf(f, "  \"total_master_%s\": %.6g,\n", k, st.accM[k] + M.get(k));
  for (auto &c : st.lockWebCodes) fprintf(f, "  \"lock_web_code_%d\": %ld,\n", c.first, c.second);
  { long over = 0; for (double x : st.rtt) if (x > 10000) over++;
    num("rtt_over_10ms", over); }
  fprintf(f, "  \"mismatch_intervals\": [");
  for (size_t i = 0; i < st.mismatch.size(); i++)
    fprintf(f, "%s[%.3f, %.0f]", i ? ", " : "", st.mismatch[i].first, st.mismatch[i].second);
  fprintf(f, "],\n");
  num("master_rx", M.get("rx")); num("master_lost", M.get("lost")); num("master_dup", M.get("dup"));
  num("master_bad", M.get("bad")); num("master_reboots_seen", M.get("reboots"));
  num("master_ack_ok", M.get("ackCount")); num("master_ack_fail", M.get("ackFail"));
  num("slave_rejected", S.get("rejected")); num("slave_dht_errors", S.get("dhtErrors"));
  num("json_overflows", M.get("jsonOverflows"));
  num("toggles", st.toggles); num("http503", st.http503); num("http403", st.http403); num("http_other", st.httpOther);
  num("cons_samples_all", st.consSamplesAll); num("cons_bad_all", st.consBadAll);
  num("wet_with_relay_on", st.wetWithRelayOn); num("lock_presses", st.lockPresses);
  num("cons_samples", st.consSamples); num("cons_bad", st.consBad); num("cons_max_ms", st.consMaxMs);
  num("wet_events", st.wetEvents); num("lock_violations", st.lockViol); num("on_attempts_locked", st.onAttemptsLocked);
  num("slave_trips", S.get("trips")); num("slave_refusals", S.get("refusals"));
  num("presses_long", st.pressesLong); num("presses_short", st.pressesShort);
  num("slave_presses0", S.get("presses0")); num("slave_presses1", S.get("presses1"));
  num("slave_edges0", S.get("edges0")); num("slave_maxedges0", S.get("maxedges0"));
  num("alarm_raise_s", st.alarmRaiseS); num("alarm_clear_s", st.alarmClearS);
  num("queue_stuck", st.queueStuck);
  auto stat = [&](const char *k, std::vector<double> v) {
    if (v.empty()) { fprintf(f, "  \"%s\": null,\n", k); return; }
    std::sort(v.begin(), v.end());
    double s = 0; for (double x : v) s += x;
    auto q = [&](double p) { return v[std::min(v.size() - 1, (size_t)(p * v.size()))]; };
    fprintf(f, "  \"%s\": {\"n\": %zu, \"min\": %.6g, \"mean\": %.6g, \"p50\": %.6g, \"p99\": %.6g, \"max\": %.6g},\n",
            k, v.size(), v.front(), s / v.size(), q(0.5), q(0.99), v.back());
  };
  stat("period_ms", st.periodMs); stat("react_us", st.reactUs); stat("rtt_us", st.rtt);
  fprintf(f, "  \"data_json\": %s\n}\n", data.c_str());
  fclose(f);
  if (!st.rtt.empty()) {
    FILE *r = fopen((out + "/" + sc.name + "_rtt.csv").c_str(), "w");
    fprintf(r, "rtt_us\n");
    for (double x : st.rtt) fprintf(r, "%.0f\n", x);
    fclose(r);
  }
  if (!st.reactUs.empty()) {
    FILE *r = fopen((out + "/" + sc.name + "_react.csv").c_str(), "w");
    fprintf(r, "react_us\n");
    for (double x : st.reactUs) fprintf(r, "%.0f\n", x);
    fclose(r);
  }
  fclose(S.log); fclose(M.log);
  printf("%s: %.1f h, %ld итерации, отчети %ld (загубени в канала %ld, главен: %g), "
         "несъответствие %ld/%ld, нарушения на защитата %ld\n",
         sc.name.c_str(), sc.hours, iter, st.reportsSent, st.reportsLostApp, M.get("lost"),
         st.consBad, st.consSamples, st.lockViol);
  return 0;
}

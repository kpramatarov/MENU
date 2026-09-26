# АНОТАЦИЯ

**Тема:** Система за управление на дома чрез IEEE 802.11 (Wi-Fi) интерфейс
**Дипломант:** инж. Кръстиян Тодоров Праматаров, ф. № 901322003
**Специалност:** Електронни системи за хибридни и електромобили
**Научен ръководител:** доц. д-р инж. Любомир Богданов

---

В дипломната работа е проектирана, реализирана и изследвана **двувъзлова микроконтролерна
система за управление на дома** с потребителски интерфейс по
стандарта IEEE 802.11, изградена върху два модула ESP8266. Подчиненият възел управлява четири
релета и има два датчика: DHT11 за температура и относителна влажност и HR202 за влага.

Проведеният литературен обзор съпоставя пет безжични технологии по десет
технико-икономически критерия. Показано е, че общоприетият довод срещу използването на
Wi-Fi в приложения от областта на Интернет на нещата – високото енергопотребление – е
валиден единствено за възли с батерийно захранване и **отпада за устройства, комутиращи
товари с мощност от порядъка на киловати**, при които делът на управляващата електроника е
под 0,05 %. Обоснована е **хибридна двуслойна комуникационна архитектура**: достъпът на
потребителя се осъществява от произволен браузър по HTTP/REST, а обменът между възлите –
по протокола ESP-NOW на връзково ниво, което премахва зависимостта от домашния
маршрутизатор.

В хардуерната част е анализирана съвместимостта на релейните модули с оптронен вход и на
двата датчика с 3,3 V логика на ESP8266 (входен ток на релейните модули около 2,2 mA на канал), изчислен е делителят за контрол на
захранващото напрежение (пълна скала 11,19 V при калибрирани 10,91 V) и е съставен
енергийният баланс при захранване по USB (около 0,49 A в най-неблагоприятния случай).
Разпределението на изводите отчита функциите им при стартиране, включително преместването
на едно от релетата от GPIO0 на GPIO15. В софтуерната част е реализиран **неблокиращ главен
цикъл** с опашка от команди, защитна блокировка при отчетена влага от датчика HR202 и вградени средства за
измерване на времето за цикъл, закъснението и надеждността на връзката. Конфигурирането по
UART със запис в емулирана енергонезависима памет с контролна сума премахва твърдо
кодираните мрежови данни от изходния код.

Устойчивостта на системата е изследвана чрез **съвместна симулация на двете непроменени
програми** с модел на радиоканала по IEEE 802.11 – 44 h в 11 сценария. Двупосочното
закъснение е 2,9…4,2 ms за 98 % от заявките. Главният възел отчита загубите точно, а
показаното състояние се възстановява със следващия отчет. Защитната блокировка не допуска
включено реле при нито едно от 495 намокряния на датчика HR202.

Системата е позиционирана като **изпълнителен слой на локален енергиен мениджмънт** при
домашно зареждане на електромобил: количествено е показано, че при еднофазно присъединяване
25 A и зареждане с 16 A остава резерв от едва 2070 W.

**Ключови думи:** IEEE 802.11, ESP8266, ESP-NOW, домашна автоматизация, енергиен
мениджмънт, електромобили, DHT11, HR202, неблокиращо програмиране, релейни модули, симулация.

Обемът на дипломната работа е 76 страници и съдържа 19 фигури, 31 таблици,
4 листинга и 41 литературни източника. Графичната част се състои от 3 листа формат A1.

---

# ABSTRACT

**Title:** Home Automation System via IEEE 802.11 (Wi-Fi) Interface

The thesis presents the design, implementation and evaluation of a **two-node microcontroller home
automation system** with an IEEE 802.11 user interface, built on two ESP8266 modules. The slave
node drives four relays and reads two sensors: a DHT11 temperature and humidity sensor and an
HR202 moisture sensor.

A comparative review of five wireless technologies against ten technical and economic
criteria shows that the common argument against Wi-Fi for Internet-of-Things applications –
its power consumption – holds only for battery-powered nodes and **does not apply to devices
switching kilowatt-range loads**, where the controller accounts for less than 0.05 % of the
total. A **hybrid two-layer communication architecture** is justified: the user connects
from any browser over HTTP/REST, while inter-node exchange uses the ESP-NOW link-layer
protocol, removing the dependency on the home router.

The hardware section analyses the compatibility of opto-isolated relay modules and of both
sensors with the 3.3 V logic of the ESP8266 (relay input current about 2.2 mA per channel), calculates the
supply-voltage divider (11.19 V full scale versus 10.91 V calibrated) and derives the power
budget for USB supply (about 0.49 A worst case). The pin assignment accounts for the boot-strap
functions of the pins, including moving one relay from GPIO0 to GPIO15. The software
implements a **non-blocking main loop** with a command queue, an HR202 moisture-triggered safety
interlock and built-in instrumentation for loop time, latency and link reliability. UART
configuration stored in emulated non-volatile memory with a checksum eliminates hard-coded
network credentials from the source code.

Robustness is studied by **co-simulating both unmodified firmware images** with an IEEE 802.11
channel model – 44 h in 11 scenarios. The round-trip time is 2.9–4.2 ms for 98 % of the
requests, the master node accounts for every detectable packet loss, the displayed state
recovers with the next report, and the safety interlock keeps all relays off in each of 495
simulated wetting events of the HR202 sensor.

The system is positioned as an **actuation layer for local home energy management** during
electric-vehicle charging: with a 25 A single-phase supply and 16 A charging, only 2070 W of
headroom remains.

**Keywords:** IEEE 802.11, ESP8266, ESP-NOW, home automation, energy management, electric
vehicles, DHT11, HR202, non-blocking programming, relay modules, simulation.

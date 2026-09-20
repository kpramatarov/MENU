# АНОТАЦИЯ

**Тема:** Система за управление на дома чрез IEEE 802.11 (Wi-Fi) интерфейс
**Дипломант:** инж. Кръстиян Тодоров Праматаров, ф. № 901322003
**Специалност:** Електронни системи за хибридни и електромобили
**Научен ръководител:** доц. д-р инж. Любомир Богданов

---

В дипломната работа е проектирана, реализирана и подготвена за експериментално изследване
**двувъзлова микроконтролерна система за управление на дома** с потребителски интерфейс по
стандарта IEEE 802.11, изградена върху два модула ESP8266.

Проведеният литературен обзор съпоставя пет безжични технологии по десет
технико-икономически критерия. Показано е, че общоприетият довод срещу използването на
Wi-Fi в приложения от областта на Интернет на нещата – високото енергопотребление – е
валиден единствено за възли с батерийно захранване и **отпада за устройства, комутиращи
товари с мощност от порядъка на киловати**, при които делът на управляващата електроника е
под 0,05 %. Обоснована е **хибридна двуслойна комуникационна архитектура**: достъпът на
потребителя се осъществява от произволен браузър по HTTP/REST, а обменът между възлите –
по протокола ESP-NOW на връзково ниво, което премахва зависимостта от домашния
маршрутизатор.

В хардуерната част са оразмерени драйверните стъпала на четирите релейни канала, като
допустимият диапазон за базовия резистор е изведен от две противоположни ограничения и
възлиза на 217…728 Ω. В софтуерната част е реализирано **изцяло неблокиращо изпълнение**
чрез крайни автомати, при което времето за един цикъл е сведено до 1…2 ms – 375 пъти
по-малко от варианта с блокиращо четене на датчика. Конфигурирането по UART със запис в
96-байтов буфер премахва твърдо кодираните мрежови данни от изходния код.

Системата е позиционирана като **изпълнителен слой на локален енергиен мениджмънт** при
домашно зареждане на електромобил: количествено е показано, че при еднофазно присъединяване
25 A и зареждане с 16 A остава резерв от едва 2070 W.

**Ключови думи:** IEEE 802.11, ESP8266, ESP-NOW, домашна автоматизация, енергиен
мениджмънт, електромобили, неблокиращо програмиране, LittleFS, RESTful API.

Обемът на дипломната работа е 61 страници и съдържа 15 фигури, 27 таблици,
11 листинга и 31 литературни източника. Графичната част се състои от 3 листа формат A1.

---

# ABSTRACT

**Title:** Home Automation System via IEEE 802.11 (Wi-Fi) Interface

The thesis presents the design and implementation of a **two-node microcontroller home
automation system** with an IEEE 802.11 user interface, built on two ESP8266 modules.

A comparative review of five wireless technologies against ten technical and economic
criteria shows that the common argument against Wi-Fi for Internet-of-Things applications –
its power consumption – holds only for battery-powered nodes and **does not apply to devices
switching kilowatt-range loads**, where the controller accounts for less than 0.05 % of the
total. A **hybrid two-layer communication architecture** is justified: the user connects
from any browser over HTTP/REST, while inter-node exchange uses the ESP-NOW link-layer
protocol, removing the dependency on the home router.

The hardware section derives the admissible base-resistor range (217…728 Ω) for the relay
driver stages from two opposing constraints. The software is **fully non-blocking**, built
on finite state machines, reducing the loop time to 1…2 ms – 375 times shorter than the
blocking-sensor variant. UART configuration stored in a 96-byte buffer eliminates hard-coded
network credentials from the source code.

The system is positioned as an **actuation layer for local home energy management** during
electric-vehicle charging: with a 25 A single-phase supply and 16 A charging, only 2070 W of
headroom remains.

**Keywords:** IEEE 802.11, ESP8266, ESP-NOW, home automation, energy management, electric
vehicles, non-blocking programming, LittleFS, RESTful API.

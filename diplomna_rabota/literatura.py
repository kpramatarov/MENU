#!/usr/bin/env python3
"""Номерира литературните източници по реда на първото им цитиране (БДС ISO 690).

Употреба:  python3 literatura.py      (от папката diplomna_rabota)

В текста източниците се цитират с номер ([11], [3–5], [11, 20]) или - за нов
източник - с ключ ([@SRD05], [@DHT11; @LM393]). Скриптът:
  1. разпознава номерата по текущия Literatura.md и ги превръща в ключове;
  2. определя реда на първото цитиране във всички глави;
  3. преномерира цитиранията и пресъздава Literatura.md, без нецитираните източници.
Кодът (``` ... ```, `...`) и формулите ($...$) не се променят.
"""
import re

FILES = ['Glava_1_Uvod_cel_zadachi.md', 'Glava_2_Harduerno_proektirane.md',
         'Glava_3_Softuerno_proektirane.md', 'Glava_4_Eksperimentalni_rezultati.md',
         'Zaklyuchenie.md']

# ключ: (разпознаващ низ в заглавието, категория, пълен запис)
#   категории: s - стандарти и спецификации, d - каталожни листа и документация
#   на елементи, o - други (програмна документация, монографии, дисертации)
SRC = {
    'EN50090': ('EN 50090', 's', 'EN 50090. *Home and Building Electronic Systems (HBES).* CENELEC.'),
    'KNX': ('ISO/IEC 14543-3', 's', 'ISO/IEC 14543-3. *Home electronic systems (HES) architecture (KNX).* ISO/IEC.'),
    'RFC7228': ('RFC 7228', 's', 'RFC 7228. *Terminology for Constrained-Node Networks.* IETF, 2014.'),
    'RFC4944': ('RFC 4944', 's', 'RFC 4944. *Transmission of IPv6 Packets over IEEE 802.15.4 Networks.* IETF, 2007.'),
    'RFC7252': ('RFC 7252', 's', 'RFC 7252. *The Constrained Application Protocol (CoAP).* IETF, 2014.'),
    'IEEE802154': ('IEEE Std 802.15.4', 's', 'IEEE Std 802.15.4-2020. *Standard for Low-Rate Wireless Networks.* IEEE, 2020.'),
    'BT53': ('Bluetooth Core Specification', 's', 'Bluetooth SIG. *Bluetooth Core Specification, Version 5.3.* 2021.'),
    'MATTER': ('Matter Specification', 's', 'Connectivity Standards Alliance. *Matter Specification, Version 1.0.* CSA, 2022.'),
    'THREAD': ('Thread 1.3', 's', 'Thread Group. *Thread 1.3 Specification.* 2022.'),
    'IEEE80211': ('IEEE Std 802.11', 's', 'IEEE Std 802.11-2020. *Part 11: Wireless LAN MAC and PHY Specifications.* IEEE, 2021.'),
    'ESP8266DS': ('ESP8266EX Datasheet', 'd', 'Espressif Systems. *ESP8266EX Datasheet.* Version 7.0, 2023.'),
    'ESPNOW': ('ESP-NOW User Guide', 'o', 'Espressif Systems. *ESP-NOW User Guide.*'),
    'IEC61851': ('IEC 61851-1', 's', 'IEC 61851-1:2017. *Electric vehicle conductive charging system – Part 1: General requirements.* IEC.'),
    'EN60898': ('EN 60898-1', 's', 'EN 60898-1. *Circuit-breakers for overcurrent protection for household and similar installations.* CENELEC.'),
    'ISO15118': ('ISO 15118-2', 's', 'ISO 15118-2:2014. *Road vehicles – Vehicle to grid communication interface – Part 2: Network and application protocol requirements.* ISO.'),
    'OCPP': ('OCPP', 's', 'Open Charge Alliance. *Open Charge Point Protocol (OCPP) 2.0.1 – Part 2: Specification.*'),
    'EN50160': ('EN 50160', 's', 'EN 50160:2010. *Voltage characteristics of electricity supplied by public distribution networks.* CENELEC.'),
    'N2222': ('2N2222', 'd', 'onsemi. *P2N2222A / 2N2222A – Amplifier Transistors NPN Silicon – Datasheet.*'),
    'DS18B20': ('DS18B20', 'd', 'Analog Devices (Maxim Integrated). *DS18B20 Programmable Resolution 1-Wire Digital Thermometer – Datasheet.* Rev. 6.'),
    'ESPHDG': ('ESP8266 Hardware Design Guidelines', 'd', 'Espressif Systems. *ESP8266 Hardware Design Guidelines.* Version 2.6, 2023.'),
    'N4007': ('1N4007', 'd', 'Vishay. *1N4001 – 1N4007 General Purpose Plastic Rectifier – Datasheet.*'),
    'AMS1117': ('AMS1117', 'd', 'Advanced Monolithic Systems. *AMS1117 – 1A Low Dropout Voltage Regulator – Datasheet.*'),
    'ARDCORE': ('Arduino core for ESP8266', 'o', 'ESP8266 Community. *Arduino core for ESP8266 – Documentation.*'),
    'NONOSSDK': ('Non-OS SDK', 'o', 'Espressif Systems. *ESP8266 Non-OS SDK API Reference.* Version 3.0.'),
    'LITTLEFS': ('littlefs', 'o', 'ARM Ltd. *littlefs – A little fail-safe filesystem designed for microcontrollers.*'),
    'FIELDING': ('Fielding', 'o', 'Fielding, R. T. *Architectural Styles and the Design of Network-based Software Architectures.* PhD dissertation, University of California, Irvine, 2000.'),
    'FETCH': ('Fetch Living Standard', 's', 'WHATWG. *Fetch Living Standard.*'),
    'DALLAS': ('DallasTemperature', 'o', 'Burton, M. *Arduino-Temperature-Control-Library (DallasTemperature) – Documentation.*'),
    'RAPPAPORT': ('Rappaport', 'o', 'Rappaport, T. S. *Wireless Communications: Principles and Practice.* 2nd ed., Prentice Hall, 2002.'),
    'ITUP1238': ('ITU-R P.1238', 's', 'ITU-R P.1238. *Propagation data and prediction methods for the planning of indoor radiocommunication systems.* International Telecommunication Union.'),
    'GUM': ('JCGM 100', 's', 'JCGM 100:2008. *Evaluation of measurement data – Guide to the expression of uncertainty in measurement (GUM).* BIPM.'),
    # --- добавени с Глава 2 (реалният макет)
    'CH340': ('CH340', 'd', 'Nanjing Qinheng Microelectronics (WCH). *CH340 – USB to Serial Chip – Datasheet.*'),
    'SRD05': ('SRD-05VDC-SL-C', 'd', 'Ningbo Songle Relay Co. *SRD Series Relay (SRD-05VDC-SL-C) – Datasheet.*'),
    'NODEMCU': ('NODEMCU DEVKIT', 'd', 'NodeMCU Team. *NODEMCU DEVKIT V1.0 – Schematic.* GitHub: nodemcu/nodemcu-devkit-v1.0, 2015.'),
    'DHT11': ('DHT11 Humidity', 'd', 'Aosong Electronics. *DHT11 Humidity & Temperature Sensor – Datasheet.*'),
    'HR202L': ('HR202L', 'd', 'Aosong Electronics. *HR202L – Humidity Sensitive Resistor – Datasheet.* 2022.'),
    'LM393': ('LM393', 'd','Texas Instruments. *LMx93-N, LM2903-N Low-Power, Low-Offset Voltage, Dual Comparators (LM393) – Datasheet.*'),
    'PC817': ('PC817', 'd', 'Sharp Corporation. *PC817 Series – DIP 4-pin General Purpose Photocoupler – Datasheet.*'),
    'GANSSLE': ('Debouncing', 'o', 'Ganssle, J. G. *A Guide to Debouncing.* The Ganssle Group, 2004 (rev. 2008).'),
    'NIELSEN': ('Usability Engineering', 'o', 'Nielsen, J. *Usability Engineering.* Boston: Academic Press, 1993.'),
    'ADADHT': ('DHT-sensor-library', 'o', 'Adafruit Industries. *DHT-sensor-library – Arduino library for DHT11, DHT22 and similar sensors.* Version 1.4.6, GitHub.'),
    'USB20': ('Universal Serial Bus Specification', 's', 'USB Implementers Forum. *Universal Serial Bus Specification, Revision 2.0.* 2000.'),
    # --- добавени с Глава 3 (програмите версия 3)
    'RFC9110': ('RFC 9110', 's', 'RFC 9110. *HTTP Semantics.* IETF, 2022.'),
    'RFC8259': ('RFC 8259', 's', 'RFC 8259. *The JavaScript Object Notation (JSON) Data Interchange Format.* IETF, 2017.'),
    'RFC6762': ('RFC 6762', 's', 'RFC 6762. *Multicast DNS.* IETF, 2013.'),
    'RFC7617': ('RFC 7617', 's', "RFC 7617. *The 'Basic' HTTP Authentication Scheme.* IETF, 2015."),
}

HEADER = """# ИЗПОЛЗВАНА ЛИТЕРАТУРА

*Източниците са номерирани по реда на първото им цитиране в текста съгласно
БДС ISO 690. Позоваванията в изложението са дадени в квадратни скоби.*
"""

CITE = re.compile(r'\[((?:@[A-Z0-9_]+(?:\s*;\s*@[A-Z0-9_]+)*)|(?:\d+(?:\s*[,–-]\s*\d+)*))\]')
PROTECT = re.compile(r'```.*?```|`[^`\n]*`|\$\$.*?\$\$|\$[^$\n]*\$', re.S)


def key_of(title):
    hits = [k for k, (sig, _, _) in SRC.items() if sig in title]
    if len(hits) != 1:
        raise SystemExit(f'Не може да се разпознае източникът: {title!r} -> {hits}')
    return hits[0]


def current_numbering():
    txt = open('Literatura.md', encoding='utf-8').read()
    return {int(n): key_of(t) for n, t in re.findall(r'^(\d+)\. (.+)$', txt, re.M)}


def parts(text):
    """Разделя текста на (защитен?, част)."""
    out, pos = [], 0
    for m in PROTECT.finditer(text):
        out.append((False, text[pos:m.start()]))
        out.append((True, m.group(0)))
        pos = m.end()
    out.append((False, text[pos:]))
    return out


def keys_in(body, num2key):
    if body.startswith('@'):
        return [k.strip()[1:] for k in body.split(';')]
    res = []
    for item in re.split(r'\s*,\s*', body):
        m = re.fullmatch(r'(\d+)\s*[–-]\s*(\d+)', item)
        nums = range(int(m.group(1)), int(m.group(2)) + 1) if m else [int(item)]
        res += [num2key[n] for n in nums]
    return res


def fmt(nums):
    nums = sorted(set(nums))
    out, i = [], 0
    while i < len(nums):
        j = i
        while j + 1 < len(nums) and nums[j + 1] == nums[j] + 1:
            j += 1
        if j - i >= 2:
            out.append(f'{nums[i]}–{nums[j]}')
        else:
            out += [str(n) for n in nums[i:j + 1]]
        i = j + 1
    return '[' + ', '.join(out) + ']'


def main():
    num2key = current_numbering()
    texts = {f: open(f, encoding='utf-8').read() for f in FILES}
    order = []
    for f in FILES:
        for prot, chunk in parts(texts[f]):
            if prot:
                continue
            for m in CITE.finditer(chunk):
                for k in keys_in(m.group(1), num2key):
                    if k not in SRC:
                        raise SystemExit(f'{f}: непознат ключ {k}')
                    if k not in order:
                        order.append(k)
    newnum = {k: i + 1 for i, k in enumerate(order)}

    for f in FILES:
        out = []
        for prot, chunk in parts(texts[f]):
            if not prot:
                chunk = CITE.sub(lambda m: fmt(newnum[k] for k in keys_in(m.group(1), num2key)), chunk)
            out.append(chunk)
        new = ''.join(out)
        if new != texts[f]:
            open(f, 'w', encoding='utf-8').write(new)
            print('  обновен:', f)

    cnt = {'s': 0, 'd': 0, 'o': 0}
    lines = [HEADER]
    for k in order:
        cnt[SRC[k][1]] += 1
        lines.append(f'{newnum[k]}. {SRC[k][2]}')
    lines += ['', '---', '',
              f'**Общо: {len(order)} заглавия** – {cnt["s"]} стандарта и спецификации, {cnt["d"]} каталожни',
              f'листа и документация на елементи и {cnt["o"]} други източника (програмна документация,',
              'монографии и дисертации).', '']
    open('Literatura.md', 'w', encoding='utf-8').write('\n'.join(lines))
    dropped = sorted(set(num2key.values()) - set(order))
    print(f'Литература: {len(order)} източника; отпаднали: {", ".join(dropped) or "няма"}')


if __name__ == '__main__':
    main()

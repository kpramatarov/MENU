#!/usr/bin/env python3
"""Фигурите за таблата (постерите) за защитата.

Употреба:  python3 tabla/generirane_figuri_tabla.py   (от папката diplomna_rabota)
Изисква:   cairosvg

Таблата са A0 хоризонтално (1189 × 841 mm = 4494 × 3179 px при 96 px/in). По
методическите указания текстът и числата трябва да са поне 24–28 pt, т. е. поне 32–37 px
при този мащаб.

1. Блокова схема – опростен вариант на Фиг. 2.1 с шрифт 40–54 px, начертан направо в
   единиците на таблото (ширина 4274 px).
2. Принципна схема – Фиг. 2.7 с шрифт ×1,4 и по-широк блок на DHT11; при ширина около
   3100 px на таблото най-дребният надпис е около 24 pt.
3. Алгоритмите (Фиг. 3.7, 3.8) и разпределението на закъснението (Фиг. 4.3) – същите
   фигури без надписа под тях (надписите са в самото табло).
PNG файловете са около 200 dpi при размера им на таблото.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

import cairosvg

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'figuri') + '/'
FIG = os.path.join(HERE, '..', 'figuri') + '/'
DPI = 200 / 96                      # PNG: около 200 dpi при размера на таблото
FONT = 'DejaVu Sans, Liberation Sans, Arial, sans-serif'
INK, NAVY, BLUE, FILL = '#16202E', '#17365D', '#2F6FA8', '#E8EDF2'


def save(name, w, h, body, poster_w):
    """SVG и PNG; poster_w - ширината на фигурата в таблото (px при 96 dpi)."""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
           f'{body}</svg>')
    path = OUT + name + '.svg'
    open(path, 'w', encoding='utf-8').write(svg)
    ET.parse(path)
    render(path, OUT + 'png/' + name + '.png', poster_w)


def render(svg_path, png_path, poster_w):
    cairosvg.svg2png(url=svg_path, write_to=png_path, output_width=round(poster_w * DPI))
    print('  ✓', os.path.basename(png_path))


# ------------------------------------------------------- 1. блокова схема
def esc(s):
    return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def text(x, y, s, size, bold=False, anchor='middle', fill=INK, italic=False):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" '
            f'font-weight="{"bold" if bold else "normal"}" font-style="{"italic" if italic else "normal"}" '
            f'text-anchor="{anchor}" fill="{fill}">{esc(s)}</text>')


def rect(x, y, w, h, sw=5, fill='#FFFFFF', dash=None, rx=16, stroke=INK):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="{sw}"{d}/>')


def poly(pts, sw=5, dash=None, end=False, start=False, color=INK):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    m = (' marker-end="url(#e)"' if end else '') + (' marker-start="url(#s)"' if start else '')
    if color != INK:
        m = m.replace('#e', '#eb').replace('#s', '#sb')
    p = ' '.join(f'{x},{y}' for x, y in pts)
    return f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{sw}"{d}{m}/>'


DEFS = ('<defs>'
        '<marker id="e" markerUnits="userSpaceOnUse" markerWidth="40" markerHeight="30" refX="38" refY="15" orient="auto">'
        f'<polygon points="0 0, 40 15, 0 30" fill="{INK}"/></marker>'
        '<marker id="s" markerUnits="userSpaceOnUse" markerWidth="40" markerHeight="30" refX="2" refY="15" orient="auto">'
        f'<polygon points="40 0, 0 15, 40 30" fill="{INK}"/></marker>'
        '<marker id="eb" markerUnits="userSpaceOnUse" markerWidth="40" markerHeight="30" refX="38" refY="15" orient="auto">'
        f'<polygon points="0 0, 40 15, 0 30" fill="{BLUE}"/></marker>'
        '<marker id="sb" markerUnits="userSpaceOnUse" markerWidth="40" markerHeight="30" refX="2" refY="15" orient="auto">'
        f'<polygon points="40 0, 0 15, 40 30" fill="{BLUE}"/></marker>'
        '</defs>')


def block(x, y, w, h, lines, fill='#FFFFFF', sizes=(50, 40, 40, 40), sw=5):
    """Блок с редове текст, центрирани по вертикала; първият ред е удебелен."""
    s = rect(x, y, w, h, sw, fill)
    gaps = [sizes[0] * 1.25] + [sizes[i] * 1.3 for i in range(1, len(lines))]
    total = sum(gaps[:len(lines)]) - gaps[0] + sizes[0]
    yy = y + (h - total) / 2 + sizes[0] * 0.8
    for i, t in enumerate(lines):
        if i:
            yy += gaps[i]
        s += text(x + w / 2, yy, t, sizes[i], i == 0)
    return s


def blokova():
    W, H = 4274, 1340
    s = DEFS + f'<rect width="{W}" height="{H}" fill="#FFFFFF"/>'
    radio = dict(dash='22 14', color=BLUE)
    # --- външни устройства
    s += rect(0, 0, 940, 1180, 4, dash='18 12', rx=24) + text(470, 70, 'ВЪНШНИ УСТРОЙСТВА', 46, True)
    s += block(60, 120, 820, 210, ['Wi-Fi рутер', 'точка за достъп'])
    s += block(60, 470, 820, 210, ['Смартфон / лаптоп', 'уеб браузър'])
    s += block(60, 860, 820, 230, ['Персонален компютър', 'сериен терминал'])
    s += poly([(470, 336), (470, 464)], **radio, end=True, start=True) + text(500, 412, 'Wi-Fi', 40, anchor='start')
    # --- главен възел
    s += rect(1180, 0, 1060, 820, 7, stroke=NAVY, rx=24) + text(1710, 70, 'ГЛАВЕН ВЪЗЕЛ – NodeMCU v3', 46, True)
    s += block(1230, 120, 960, 290, ['ESP8266EX', 'Xtensa LX106 · 80 MHz', '802.11 b/g/n · уеб сървър'], FILL)
    s += block(1230, 450, 460, 170, ['USB–UART', 'мост CH340'], sizes=(46, 40))
    s += block(1730, 450, 460, 170, ['Flash 4 MB', 'програма, EEPROM'], sizes=(46, 40))
    s += block(1230, 660, 960, 120, ['Стабилизатор AMS1117 · 3,3 V'], sizes=(44,))
    s += block(1230, 950, 960, 140, ['USB 5 V – адаптер или компютър'], sizes=(44,))
    s += poly([(1710, 950), (1710, 786)], end=True)
    s += poly([(880, 225), (1224, 225)], **radio, end=True, start=True)
    s += text(1060, 200, '802.11', 40) + text(1060, 282, 'STA', 40)
    s += poly([(880, 975), (1060, 975), (1060, 535), (1224, 535)], end=True) + text(1000, 950, 'USB', 40)
    # --- подчинен възел
    s += rect(2520, 0, 1754, 1250, 7, stroke=NAVY, rx=24) + text(3397, 70, 'ПОДЧИНЕН ВЪЗЕЛ', 46, True)
    s += block(2570, 120, 720, 630, ['NodeMCU v3', 'ESP8266EX · 80 MHz', 'USB–UART CH340',
                                     'Flash 4 MB · EEPROM', 'стабилизатор 3,3 V'], FILL, sizes=(54, 40, 40, 40, 40))
    s += block(2570, 950, 720, 140, ['USB 5 V – адаптер ≥ 1 A'], sizes=(44,))
    s += poly([(2930, 950), (2930, 756)], end=True)
    s += poly([(2190, 265), (2564, 265)], **radio, end=True, start=True)
    s += text(2380, 240, 'ESP-NOW', 44, True) + text(2380, 320, '2,4 GHz', 40)
    # периферия
    px, pw = 3480, 700
    per = [(120, 130, ['DHT11', 'температура, влажност'], 'D4', 'both'),
           (275, 130, ['HR202 + LM393', 'датчик за влага'], 'D7', 'in'),
           (430, 130, ['2 × бутон', 'SB1, SB2'], 'D5 D6', 'in'),
           (585, 130, ['Делител 47k / 20k', 'контрол на +5 V'], 'A0', 'in'),
           (740, 200, ['4 × релеен модул', 'входове D0, D1, D2, D8', 'контакти – свободни'], None, 'out'),
           (965, 130, ['C1 1000 µF / 16 V', 'буфер на +5 V'], None, None)]
    for y, h, lines, pin, d in per:
        s += block(px, y, pw, h, lines, sizes=(46, 40, 40))
        cy = y + h / 2
        if d in ('both', 'in'):
            s += poly([(px - 6, cy), (3296, cy)], end=True, start=(d == 'both')) + text(3385, cy - 16, pin, 40, True)
        elif d == 'out':
            s += poly([(3290, 700), (3385, 700), (3385, cy), (px - 6, cy)], end=True)
    # шина +5 V (VIN) от адаптера към делителя, релетата и C1
    bx = 4225
    s += poly([(2930, 1090), (2930, 1160), (bx, 1160), (bx, 650)])
    for yy in (650, 840, 1030):
        s += poly([(bx, yy), (px + pw + 6, yy)], end=True) + f'<circle cx="{bx}" cy="{yy}" r="9" fill="{INK}"/>'
    s += text(3690, 1145, '+5 V (VIN)', 40, True)
    # конфигуриране на подчинения възел по USB
    s += poly([(470, 1090), (470, 1215), (2545, 1215), (2545, 650), (2564, 650)], end=True)
    s += text(1500, 1195, 'USB – конфигуриране на подчинения възел', 40)
    # означения
    s += poly([(0, 1300), (150, 1300)], end=True) + text(175, 1314, 'електрическа връзка', 40, anchor='start')
    s += poly([(640, 1300), (790, 1300)], **radio, end=True, start=True)
    s += text(815, 1314, 'радиовръзка 2,4 GHz', 40, anchor='start')
    s += text(1290, 1314, 'Контактите на релетата не са свързани към мрежата 230 V.', 40, True, 'start')
    save('tablo_blokova_shema', W, H, s, W)


# ------------------------------------------------------- 2. принципна схема
def principna():
    """Фиг. 2.7 с шрифт ×1,4. Кодът повтаря fig_2_7() от figuri/generirane_figuri_gl2.py,
    с по-широки блокове на датчиците и без надписа под фигурата."""
    sys.path.insert(0, FIG)
    import generirane_figuri_gl2 as g
    K = 1.4
    L, R, dot, ring, gnd, T0 = g.L, g.R, g.dot, g.ring, g.gnd, g.T

    def T(x, y, s, fs=11, b=False, an='middle', fill='#000', it=False):
        return T0(x, y, s, fs * K, b, an, fill, it)

    def net(x, y, name, side='r', stub=26):
        w, h = 12 + 8 * K * len(name), 21
        if side == 'r':
            return L(x, y, x + stub, y) + R(x + stub, y - h / 2, w, h, 1.2) + T(x + stub + w / 2, y + 5.5, name, 10.5, True)
        return L(x - stub, y, x, y) + R(x - stub - w, y - h / 2, w, h, 1.2) + T(x - stub - w / 2, y + 5.5, name, 10.5, True)

    g.T, g.net = T, net                 # res_v, cap_v и button_v пишат надписите си с T
    W, H = 1300, 905
    s = g.DEFS + f'<rect width="{W}" height="{H}" fill="#fff"/>'
    bx, by, bw, bh = 560, 110, 230, 540
    s += R(bx, by, bw, bh, 2) + T(bx + bw / 2, by + 30, 'DD1', 12, True)
    s += T(bx + bw / 2, by + 50, 'NodeMCU v3', 11, True) + T(bx + bw / 2, by + 68, '(ESP8266EX)', 10)
    s += R(bx + 80, by - 30, 70, 30, 1.5) + T(bx + 115, by - 9, 'USB', 10, True)
    s += T(bx + 115, by - 42, 'XS1 – 5 V от адаптер', 10, it=True)
    left = [('VIN', 200), ('GND', 260), ('3V3', 320), ('A0', 380), ('RX', 480), ('TX', 520)]
    right = [('D0', 200), ('D1', 250), ('D2', 300), ('D8', 350), ('D4', 420), ('D7', 470), ('D5', 540), ('D6', 590)]
    gpio = {'D0': 'GPIO16', 'D1': 'GPIO5', 'D2': 'GPIO4', 'D8': 'GPIO15', 'D4': 'GPIO2',
            'D7': 'GPIO13', 'D5': 'GPIO14', 'D6': 'GPIO12'}
    for n, yy in left:
        s += T(bx + 8, yy + 5, n, 10.5, True, 'start')
    for n, yy in right:
        s += T(bx + bw - 8, yy + 5, f'{n} ({gpio[n]})', 10, True, 'end')
    s += net(bx, 200, '+5 V', 'l') + L(bx - 30, 260, bx, 260) + gnd(bx - 30, 260)
    s += net(bx, 320, '+3,3 V', 'l') + net(bx, 380, 'A0', 'l')
    s += L(bx - 40, 480, bx, 480) + L(bx - 40, 520, bx, 520)
    s += T(bx - 46, 494, 'UART – свободни', 10, an='end', it=True) + T(bx - 46, 514, 'за конфигуриране', 10, an='end', it=True)
    for n, yy in right:
        s += net(bx + bw, yy, n, 'r')
    x1 = 120
    s += net(x1, 140, '+5 V', 'l', 0)
    s += L(x1, 140, x1, 160) + g.res_v(x1, 160, 260, 'R1', '47 kΩ', 'r') + dot(x1, 275) + L(x1, 260, x1, 290)
    s += g.res_v(x1, 290, 400, 'R2', '20 kΩ', 'r') + L(x1, 275, 200, 275) + net(200, 275, 'A0', 'r', 0)
    s += gnd(x1, 400) + T(x1, 440, 'към GND на DD1', 9.5, it=True) + T(x1, 458, 'с отделен проводник', 9.5, it=True)
    x2 = 330
    s += net(x2, 140, '+5 V', 'l', 0) + L(x2, 140, x2, 220)
    s += g.cap_v(x2, 220, 340, 'C1', '1000 µF / 16 V', True, 'r') + gnd(x2, 340)
    mx, mw, mh = 950, 190, 112
    for i, (pin, yy) in enumerate((('D0', 110), ('D1', 240), ('D2', 370), ('D8', 500))):
        s += R(mx, yy, mw, mh, 1.6) + T(mx + mw / 2, yy + 20, f'A{i + 1} – релеен модул', 10.5, True)
        s += T(mx + mw / 2, yy + 37, 'джъмпер H', 9.5, it=True)
        for k, (lbl, net_name) in enumerate((('VCC', '+5 V'), ('IN', pin), ('GND', 'GND'))):
            py = yy + 55 + k * 23
            s += T(mx + 8, py + 5, lbl, 9.5, an='start') + net(mx, py, net_name, 'l', 14)
        for k, lbl in enumerate(('NO', 'COM', 'NC')):
            py = yy + 55 + k * 23
            s += T(mx + mw - 8, py + 5, lbl, 9.5, an='end') + L(mx + mw, py, mx + mw + 16, py) + ring(mx + mw + 20, py, 4)
    # датчици (по-широки блокове, за да се побере текстът) и бутони
    s += R(405, 715, 215, 115, 1.6) + T(512, 735, 'BK1 – DHT11', 10.5, True)
    s += T(512, 752, 'температура и влажност', 9.5, it=True)
    for k, (lbl, nn) in enumerate((('VCC', '+3,3 V'), ('DATA', 'D4'), ('GND', 'GND'))):
        py = 772 + k * 22
        s += T(413, py + 5, lbl, 9.5, an='start') + net(405, py, nn, 'l', 14)
    s += R(655, 715, 205, 115, 1.6) + T(757, 735, 'A5 – HR202 + LM393', 10.5, True)
    s += T(757, 752, 'датчик за влага', 9.5, it=True)
    for k, (lbl, nn) in enumerate((('VCC', '+3,3 V'), ('DO', 'D7'), ('GND', 'GND'))):
        py = 772 + k * 22
        s += T(852, py + 5, lbl, 9.5, an='end') + net(860, py, nn, 'r', 14)
    for xk, nn, lbl in ((1080, 'D5', 'SB1'), (1190, 'D6', 'SB2')):
        s += net(xk, 710, nn, 'l', 0) + L(xk, 710, xk, 720) + g.button_v(xk, 720, 810, lbl) + gnd(xk, 810)
    s += T(60, 866, 'Всички релейни модули са с джъмпер в положение H. Връзките са означени с имената на веригите.', 10, an='start')
    s += T(60, 890, 'Контактите NO, COM, NC са свободни – в макета не са свързани към мрежата 230 V.', 10, an='start')
    save('tablo_principna_shema', W, H, s, 3100)


# ------------------------------------------------- 3. фигури без надписа
def bez_nadpis(src, name, poster_w):
    s = open(FIG + src + '.svg', encoding='utf-8').read()
    m = re.search(r'<text x="[^"]*" y="([^"]*)"[^>]*>Фиг\.[^<]*</text>', s)
    y = float(m.group(1))
    s = s.replace(m.group(0), '')
    w = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', s)
    new_h = round(y - 22)
    s = s.replace(w.group(0), f'viewBox="0 0 {w.group(1)} {new_h}"', 1)
    s = re.sub(r'(<svg[^>]*?) height="[\d.]+"', rf'\1 height="{new_h}"', s, 1)
    s = re.sub(r'<rect width="([\d.]+)" height="[\d.]+" fill="#fff"/>', rf'<rect width="\1" height="{new_h}" fill="#fff"/>', s, 1)
    path = OUT + name + '.svg'
    open(path, 'w', encoding='utf-8').write(s)
    ET.parse(path)
    render(path, OUT + 'png/' + name + '.png', poster_w)


if __name__ == '__main__':
    os.makedirs(OUT + 'png', exist_ok=True)
    blokova()
    principna()
    bez_nadpis('fig_3_7_algoritam_master', 'tablo_algoritam_master', 2080)
    bez_nadpis('fig_3_8_algoritam_slave', 'tablo_algoritam_slave', 2080)
    bez_nadpis('fig_4_3_rtt_histogram', 'tablo_rtt_histogram', 2080)

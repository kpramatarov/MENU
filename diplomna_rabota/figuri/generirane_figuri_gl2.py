#!/usr/bin/env python3
"""Генерира фигурите на Глава 2 (SVG + PNG).
Употреба:  python3 figuri/generirane_figuri_gl2.py   (от папката diplomna_rabota)
Изисква:   cairosvg; снимката на макета е в figuri/foto/maket.png
"""
import base64
import os
import xml.etree.ElementTree as ET

import cairosvg

FONT = 'DejaVu Sans, Liberation Sans, Arial, sans-serif'
OUT = 'figuri/'
DEFS = ('<defs>'
        '<marker id="a" markerWidth="9" markerHeight="7" refX="8.5" refY="3.5" orient="auto">'
        '<polygon points="0 0, 9 3.5, 0 7" fill="#000"/></marker>'
        '<marker id="as" markerWidth="9" markerHeight="7" refX="0.5" refY="3.5" orient="auto">'
        '<polygon points="9 0, 0 3.5, 9 7" fill="#000"/></marker>'
        '</defs>')


# ------------------------------------------------------------------ примитиви
def T(x, y, s, fs=11, b=False, an='middle', fill='#000', it=False):
    s = str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{fs}" '
            f'font-weight="{"bold" if b else "normal"}" font-style="{"italic" if it else "normal"}" '
            f'text-anchor="{an}" fill="{fill}">{s}</text>')


def R(x, y, w, h, sw=1.5, fill='#fff', dash=None, rx=0):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
            f'stroke="#000" stroke-width="{sw}"{d}/>')


def L(x1, y1, x2, y2, sw=1.5, dash=None, end=False, start=False):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    m = (' marker-end="url(#a)"' if end else '') + (' marker-start="url(#as)"' if start else '')
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#000" stroke-width="{sw}"{d}{m}/>'


def P(pts, sw=1.5, dash=None, end=False, start=False, fill='none'):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    m = (' marker-end="url(#a)"' if end else '') + (' marker-start="url(#as)"' if start else '')
    s = ' '.join(f'{x},{y}' for x, y in pts)
    return f'<polyline points="{s}" fill="{fill}" stroke="#000" stroke-width="{sw}"{d}{m}/>'


def dot(x, y, r=3.2):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="#000"/>'


def ring(x, y, r=4):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="#fff" stroke="#000" stroke-width="1.4"/>'


def gnd(x, y):
    """Символ за маса; (x, y) е точката на свързване отгоре."""
    return (L(x, y, x, y + 10) + L(x - 11, y + 10, x + 11, y + 10, 1.8) +
            L(x - 7, y + 15, x + 7, y + 15, 1.6) + L(x - 3, y + 20, x + 3, y + 20, 1.4))


def supply(x, y, lbl, up=True):
    """Стрелка/черта за захранване с надпис; (x, y) е точката на свързване."""
    return L(x - 12, y, x + 12, y, 2.2) + T(x, y - 7 if up else y + 18, lbl, 11, True)


def res_v(x, y1, y2, lbl='', val='', side='r', w=12, h=34):
    """Вертикален резистор (правоъгълник по IEC) между y1 и y2 с изводи."""
    ym = (y1 + y2) / 2
    s = L(x, y1, x, ym - h / 2) + R(x - w / 2, ym - h / 2, w, h, 1.5) + L(x, ym + h / 2, x, y2)
    dx = w / 2 + 6
    if side == 'r':
        s += T(x + dx, ym - 2, lbl, 11, True, 'start') + T(x + dx, ym + 12, val, 10, an='start')
    else:
        s += T(x - dx, ym - 2, lbl, 11, True, 'end') + T(x - dx, ym + 12, val, 10, an='end')
    return s


def res_h(x1, x2, y, lbl='', val='', w=34, h=12):
    xm = (x1 + x2) / 2
    s = L(x1, y, xm - w / 2, y) + R(xm - w / 2, y - h / 2, w, h, 1.5) + L(xm + w / 2, y, x2, y)
    return s + T(xm, y - h / 2 - 5, lbl, 11, True) + T(xm, y + h / 2 + 13, val, 10)


def cap_v(x, y1, y2, lbl='', val='', pol=False, side='r'):
    ym = (y1 + y2) / 2
    s = L(x, y1, x, ym - 4) + L(x - 13, ym - 4, x + 13, ym - 4, 2.2)
    if pol:
        s += (f'<path d="M {x - 13} {ym + 8} Q {x} {ym + 1} {x + 13} {ym + 8}" fill="none" '
              f'stroke="#000" stroke-width="2.2"/>') + L(x, ym + 5, x, y2) + T(x - 18, ym - 8, '+', 12, True)
    else:
        s += L(x - 13, ym + 4, x + 13, ym + 4, 2.2) + L(x, ym + 4, x, y2)
    an, dx = ('start', 20) if side == 'r' else ('end', -20)
    return s + T(x + dx, ym - 2, lbl, 11, True, an) + T(x + dx, ym + 12, val, 10, an=an)


def diode_v(x, y1, y2, up=True, lbl='', side='r'):
    """Вертикален диод; up=True - анодът е долу (катодът горе)."""
    ym = (y1 + y2) / 2
    if up:
        tri = f'<polygon points="{x - 9},{ym + 7} {x + 9},{ym + 7} {x},{ym - 7}" fill="#fff" stroke="#000" stroke-width="1.5"/>'
        bar = L(x - 9, ym - 7, x + 9, ym - 7, 1.8)
    else:
        tri = f'<polygon points="{x - 9},{ym - 7} {x + 9},{ym - 7} {x},{ym + 7}" fill="#fff" stroke="#000" stroke-width="1.5"/>'
        bar = L(x - 9, ym + 7, x + 9, ym + 7, 1.8)
    an, dx = ('start', 14) if side == 'r' else ('end', -14)
    return L(x, y1, x, ym - 7) + tri + bar + L(x, ym + 7, x, y2) + T(x + dx, ym + 4, lbl, 10.5, True, an)


def led_v(x, y1, y2, lbl='', side='r'):
    s = diode_v(x, y1, y2, up=False, lbl='', side=side)
    ym = (y1 + y2) / 2
    s += T(x + 28, ym + 4, lbl, 10.5, True, 'start')
    for k in (0, 8):
        s += L(x + 11, ym - 6 + k, x + 20, ym - 13 + k, 1.2, end=True)
    return s


def npn(x, y, lbl='', side='r'):
    """NPN транзистор: база вляво в (x-22, y), колектор горе (x+12, y-26), емитер долу (x+12, y+26)."""
    s = (f'<circle cx="{x + 4}" cy="{y}" r="20" fill="#fff" stroke="#000" stroke-width="1.3"/>' +
         L(x - 22, y, x - 6, y) + L(x - 6, y - 12, x - 6, y + 12, 2.4) +
         L(x - 6, y - 6, x + 12, y - 18) + L(x + 12, y - 18, x + 12, y - 26) +
         L(x - 6, y + 6, x + 12, y + 18, end=True) + L(x + 12, y + 18, x + 12, y + 26))
    return s + T(x + 30, y + 4, lbl, 10.5, True, 'start' if side == 'r' else 'end')


def switch_no(x, y1, y2):
    """Нормално отворен контакт (вертикален)."""
    return (L(x, y1, x, y1 + 12) + ring(x, y1 + 12, 2.5) + L(x, y1 + 12, x + 14, y2 - 14, 1.8) +
            ring(x, y2 - 12, 2.5) + L(x, y2 - 12, x, y2))


def button_v(x, y1, y2, lbl=''):
    """Бутон (нормално отворен) с натискател; вертикален."""
    ym = (y1 + y2) / 2
    s = (L(x, y1, x, ym - 12) + dot(x, ym - 12, 2.4) + L(x, ym + 12, x, y2) + dot(x, ym + 12, 2.4) +
         L(x - 2, ym + 12, x - 16, ym - 12, 1.8) + L(x - 9, ym, x - 26, ym, 1.4, '3 2') +
         L(x - 26, ym - 6, x - 26, ym + 6, 1.6))
    return s + T(x + 10, ym + 4, lbl, 11, True, 'start')


def caption(W, y, s):
    return T(W / 2, y, s, 14, True)


def write(name, W, H, body):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
           f'width="{W}" height="{H}" viewBox="0 0 {W} {H}">{DEFS}'
           f'<rect width="{W}" height="{H}" fill="#fff"/>{body}</svg>')
    p = OUT + name + '.svg'
    open(p, 'w', encoding='utf-8').write(svg)
    ET.parse(p)                                             # проверка за валиден XML
    cairosvg.svg2png(url=p, write_to=OUT + 'png/' + name + '.png', scale=2.6)
    print('  ✓', name)


# ============================================================================
#  Фиг. 2.1 - блокова схема на хардуера
# ============================================================================
def fig_2_1():
    W, H = 1300, 750
    s = ''

    def blk(x, y, w, h, t1, t2='', t3='', fill='#fff', fs=11):
        r = R(x, y, w, h, 1.5, fill)
        cy = y + h / 2 - (8 if t2 else 0) - (7 if t3 else 0) + 4
        r += T(x + w / 2, cy, t1, fs, True)
        if t2:
            r += T(x + w / 2, cy + 16, t2, 9.5)
        if t3:
            r += T(x + w / 2, cy + 31, t3, 9.5)
        return r

    # --- външни устройства
    s += R(20, 30, 215, 290, 1.6, dash='9 5', rx=6) + T(127.5, 52, 'ВЪНШНИ УСТРОЙСТВА', 12.5, True)
    s += blk(35, 62, 185, 55, 'Wi-Fi рутер', 'точка за достъп')
    s += blk(35, 150, 185, 52, 'Смартфон / лаптоп', 'уеб браузър')
    s += blk(35, 240, 185, 60, 'Персонален компютър', 'сериен терминал')
    s += L(127, 117, 127, 150, 1.5, '7 4', True, True) + T(136, 139, '802.11', 9.5, an='start')

    # --- главен възел
    s += R(290, 30, 290, 290, 2.2, rx=6) + T(435, 52, 'ГЛАВЕН ВЪЗЕЛ (NodeMCU v3)', 12.5, True)
    s += blk(305, 62, 260, 88, 'ESP8266EX', 'Xtensa LX106 · 80 MHz', '802.11 b/g/n · вградена антена', '#eef2f5')
    s += blk(305, 172, 125, 60, 'USB–UART', 'мост CH340')
    s += blk(440, 172, 125, 60, 'SPI Flash 4 MB', 'програма и уеб', 'страница, EEPROM*')
    s += blk(305, 252, 260, 50, 'LDO AMS1117 · 3,3 V')
    s += L(367, 172, 367, 150, 1.5, end=True, start=True) + T(375, 165, 'UART', 9.5, an='start')
    s += L(502, 172, 502, 150, 1.5, end=True, start=True) + T(510, 165, 'SPI', 9.5, an='start')
    s += L(435, 252, 435, 150, 1.5, end=True) + T(439, 244, '3,3 V', 9, an='start')
    s += L(220, 89, 305, 89, 1.5, '7 4', True, True) + T(262, 72, '802.11', 9.5) + T(262, 83, 'STA', 9.5)
    s += P([(220, 270), (268, 270), (268, 202), (305, 202)], 1.5, end=True) + T(252, 263, 'USB', 9)
    s += blk(305, 330, 260, 36, 'USB 5 V – адаптер или компютър', fs=10)
    s += L(435, 330, 435, 302, 1.5, end=True)

    # --- подчинен възел
    s += R(650, 30, 630, 440, 2.2, rx=6) + T(965, 52, 'ПОДЧИНЕН ВЪЗЕЛ', 12.5, True)
    s += R(662, 62, 262, 250, 1.4, '#fafafa', '6 4', 4) + T(793, 80, 'NodeMCU v3', 11, True)
    s += blk(674, 88, 238, 80, 'ESP8266EX', 'Xtensa LX106 · 80 MHz', '802.11 b/g/n · вградена антена', '#eef2f5')
    s += blk(674, 186, 114, 56, 'USB–UART', 'мост CH340', fs=10.5)
    s += blk(798, 186, 114, 56, 'SPI Flash 4 MB', 'програма,', 'EEPROM*', fs=10.5)
    s += blk(674, 258, 238, 42, 'LDO AMS1117 · 3,3 V', fs=10.5)
    s += L(731, 186, 731, 168, 1.5, end=True, start=True) + T(739, 180, 'UART', 9, an='start')
    s += L(855, 186, 855, 168, 1.5, end=True, start=True) + T(863, 180, 'SPI', 9, an='start')
    s += L(793, 258, 793, 168, 1.5, end=True) + T(797, 252, '3,3 V', 9, an='start')
    s += blk(674, 400, 238, 40, 'USB 5 V – адаптер ≥ 1 A', fs=10)
    s += L(793, 400, 793, 300, 1.5, end=True)
    # ESP-NOW
    s += L(565, 128, 674, 128, 1.8, '7 4', True, True) + T(620, 118, 'ESP-NOW', 10, True) + T(620, 146, '2,4 GHz', 9.5)
    # компютър -> CH340 на подчинения
    s += P([(220, 290), (250, 290), (250, 382), (640, 382), (640, 214), (674, 214)], 1.3, end=True)
    s += T(445, 398, 'USB – конфигуриране на подчинения възел', 9.5)
    # периферия
    px, pw = 1000, 245
    s += blk(px, 74, pw, 44, 'Датчик DHT11', 'температура · влажност · 3,3 V')
    s += blk(px, 126, pw, 44, 'Датчик HR202', 'влага · компаратор LM393 · 3,3 V')
    s += blk(px, 182, pw, 40, '2 × бутон', 'вграден подтеглящ резистор')
    s += blk(px, 234, pw, 40, 'Делител 47 kΩ / 20 kΩ', 'контрол на шината +5 V')
    s += blk(px, 282, pw, 40, 'C1 = 1000 µF / 16 V', 'буфер на шината +5 V')
    s += blk(px, 330, pw, 60, '4 × релеен модул', 'оптрон · ключ · реле 5 V / 10 A')
    # сигнални връзки (без пресичания: по-късните изходи завиват по-рано)
    s += L(912, 96, px, 96, 1.5, end=True, start=True) + T(956, 90, 'D4', 9.5)
    s += P([(px, 148), (975, 148), (975, 112), (912, 112)], 1.5, end=True) + T(988, 142, 'D7', 9)
    s += P([(px, 202), (960, 202), (960, 128), (912, 128)], 1.5, end=True) + T(980, 196, 'D5 D6', 9)
    s += P([(px, 254), (945, 254), (945, 144), (912, 144)], 1.5, end=True) + T(972, 248, 'A0', 9)
    s += P([(912, 160), (930, 160), (930, 360), (px, 360)], 1.5, end=True) + T(965, 354, 'D0 D1 D2 D8', 9)
    # шина +5 V (VIN)
    bx = 1264
    s += L(bx, 240, bx, 360, 1.8)
    for yy in (254, 302, 360):
        s += L(bx, yy, px + pw, yy, 1.5, end=True) + dot(bx, yy) if yy != 360 else L(bx, yy, px + pw, yy, 1.5, end=True)
    s += T(bx, 222, '+5 V', 10, True) + T(bx, 234, 'VIN', 9)

    # --- граница на галваничното разделяне и силова част
    s += L(650, 486, 1280, 486, 1.3, '14 4 2 4')
    s += T(660, 481, 'граница на галваничното разделяне – изолация бобина – контакти 1500 V~', 9.5, an='start', it=True)
    s += L(1122, 390, 1122, 545, 1.6, '2 4')
    s += T(1128, 430, 'механична', 9, an='start', it=True) + T(1128, 442, 'връзка', 9, an='start', it=True)
    s += R(650, 500, 630, 190, 1.6, dash='9 5', rx=6) + T(820, 522, 'СИЛОВА ЧАСТ · ~230 V', 12.5, True)
    s += blk(680, 545, 190, 58, 'Мрежа', '~230 V · 50 Hz')
    s += blk(1035, 545, 175, 58, 'Контакти K1…K4', 'COM · NO · NC')
    s += blk(1035, 625, 175, 50, 'Прекъсваеми товари', 'бойлер · климатик')
    s += L(870, 574, 1035, 574, 1.5, end=True) + L(1122, 603, 1122, 625, 1.5, end=True)

    # --- означения и бележки
    s += R(20, 510, 590, 180, 1.2, rx=4) + T(38, 534, 'Означения', 11.5, True, 'start')
    s += L(40, 558, 110, 558, 1.5, end=True) + T(122, 562, 'електрическа връзка (стрелката – посока на сигнала)', 10, an='start')
    s += L(40, 584, 110, 584, 1.5, '7 4', True, True) + T(122, 588, 'радиовръзка в обхвата 2,4 GHz', 10, an='start')
    s += L(40, 610, 110, 610, 1.6, '2 4') + T(122, 614, 'механична връзка', 10, an='start')
    s += T(38, 642, '*  EEPROM не е отделна схема – емулира се в сектор от SPI Flash паметта.', 10, an='start')
    s += T(38, 662, 'Двата възела се захранват независимо по USB. Бобините на релетата се захранват', 10, an='start')
    s += T(38, 678, 'непосредствено от шината +5 V (VIN), без да минават през стабилизатора.', 10, an='start')
    s += caption(W, 730, 'Фиг. 2.1. Блокова схема на хардуера')
    write('fig_2_1_blokova_shema', W, H, s)



# ============================================================================
#  Фиг. 2.2 - типична схема на релеен модул с оптронен вход (положение H)
# ============================================================================
def fig_2_2():
    W, H = 960, 560
    s = ''
    yv, yg = 80, 450
    s += L(60, yv, 890, yv, 2) + L(60, yg, 890, yg, 2)
    # съединител X1
    for yy, lbl in ((yv, 'VCC'), (260, 'IN'), (yg, 'GND')):
        s += ring(56, yy, 5) + T(44, yy + 4, lbl, 11, True, 'end')
    s += T(56, 48, 'X1', 11, True) + T(890, yv - 8, '+5 V', 11, True, 'end')
    # вход: R1 -> светодиод на оптрона -> джъмпер H -> маса
    s += res_h(61, 205, 260, 'R1', '1 kΩ')
    s += L(205, 260, 232, 260) + L(232, 260, 232, 268)
    s += R(212, 232, 150, 135, 1.3, 'none', '6 4', 4) + T(287, 226, 'U1 (PC817)', 11, True)
    s += f'<polygon points="{224},{272} {240},{272} {232},{288}" fill="#fff" stroke="#000" stroke-width="1.5"/>' + L(222, 288, 242, 288, 1.8)
    s += L(232, 288, 232, 380)
    s += L(250, 274, 280, 274, 1.2, end=True) + L(250, 286, 280, 286, 1.2, end=True)
    # фототранзистор
    cx, cy = 316, 280
    s += f'<circle cx="{cx}" cy="{cy}" r="21" fill="#fff" stroke="#000" stroke-width="1.3"/>'
    s += L(cx - 8, cy - 12, cx - 8, cy + 12, 2.4)
    s += L(cx - 8, cy - 6, cx + 10, cy - 18) + L(cx + 10, cy - 18, cx + 10, yv)
    s += L(cx - 8, cy + 6, cx + 10, cy + 18, end=True) + L(cx + 10, cy + 18, cx + 10, 330)
    s += dot(cx + 10, yv)
    # джъмпер J1: L - COM - H, мостче COM-H
    jy = 392
    for jx, lbl in ((192, 'L'), (232, 'COM'), (272, 'H')):
        s += R(jx - 7, jy - 7, 14, 14, 1.3) + T(jx, jy + 24, lbl, 9.5, True)
    s += f'<rect x="{225}" y="{jy - 12}" width="{54}" height="24" rx="6" fill="none" stroke="#000" stroke-width="2"/>'
    s += L(232, 380, 232, jy - 7) + L(272, jy + 7, 272, yg) + dot(272, yg)
    s += L(192, jy - 7, 192, 350, 1.2, '4 3') + T(186, 344, 'L – виж текста', 9, an='end', it=True)
    s += T(232, jy + 40, 'J1 (показано H)', 10, True)
    # R2 към базата на VT1
    s += L(cx + 10, 330, cx + 10, 360) + res_h(cx + 10, 440, 360, 'R2', '1 kΩ')
    s += npn(462, 360, 'VT1', 'r')
    s += L(474, 386, 474, yg) + dot(474, yg)
    # бобина K1 между +5 V и колектора
    kx = 474
    s += L(kx, yv, kx, 190) + R(kx - 16, 190, 32, 80, 1.6) + T(kx + 24, 212, 'K1', 12, True, 'start')
    s += T(kx + 24, 264, '70 Ω', 10, an='start') + L(kx, 270, kx, 334) + dot(kx, yv)
    # защитен диод VD1 успоредно на бобината (катодът към +5 V)
    s += L(kx, 160, 410, 160) + L(kx, 300, 410, 300) + dot(kx, 160) + dot(kx, 300)
    s += diode_v(410, 160, 300, up=True, lbl='VD1', side='l')
    # контакти
    s += L(kx + 16, 236, 780, 236, 1.4, '2 4')
    s += ring(870, 180, 4) + ring(870, 320, 4)
    s += L(874, 180, 905, 180) + L(874, 320, 905, 320) + ring(910, 180, 5) + ring(910, 320, 5)
    s += T(922, 184, 'NO', 11, True, 'start') + T(922, 324, 'NC', 11, True, 'start')
    s += ring(800, 250, 4) + L(803, 253, 864, 314, 2)
    s += L(800, 254, 800, 400) + L(800, 400, 905, 400) + ring(910, 400, 5) + T(922, 404, 'COM', 11, True, 'start')
    s += T(912, 150, 'X2', 11, True) + T(770, 228, 'механична връзка', 9, an='end', it=True)
    s += T(905, 438, 'към товара, до 250 V~ / 10 A', 9.5, an='end', it=True)
    s += T(60, 492, 'Показано е положение H на джъмпера J1: светодиодът на оптрона свети при високо ниво на IN.', 10, an='start')
    s += T(60, 508, 'Типична схема на модулите от този клас – производителят не публикува принципна схема.', 10, an='start')
    s += caption(W, 544, 'Фиг. 2.2. Типична схема на релеен модул с оптронен вход')
    write('fig_2_2_releen_modul', W, H, s)


# ============================================================================
#  Фиг. 2.3 - свързване на бутоните
# ============================================================================
def fig_2_3():
    W, H = 940, 440
    s = ''
    # а) прието решение
    s += T(215, 36, 'а) Прието решение', 12.5, True)
    s += R(260, 60, 200, 290, 1.4, '#fafafa', '6 4', 4) + T(360, 80, 'ESP8266', 11, True)
    s += supply(380, 110, '+3,3 V') + res_v(380, 110, 205, 'R', 'вграден', w=12, h=30)
    s += L(380, 205, 380, 220) + dot(380, 220) + L(260, 220, 380, 220)
    s += L(380, 220, 420, 220, end=True) + T(424, 244, 'към логиката', 9.5, an='end', it=True)
    s += ring(260, 220, 4) + T(254, 212, 'D5 (GPIO14)', 10.5, True, 'end')
    s += L(256, 220, 150, 220) + button_v(150, 220, 320, 'SB1') + gnd(150, 320)
    s += T(360, 370, 'INPUT_PULLUP, програмен филтър 60 ms', 10, it=True)
    # б) алтернатива с RC филтър
    s += T(700, 36, 'б) Алтернатива: RC филтър', 12.5, True)
    nx, ny = 600, 220
    s += supply(nx, 90, '+3,3 V') + res_v(nx, 90, ny, 'Rп', '10 kΩ', 'l')
    s += dot(nx, ny) + cap_v(nx, ny, 340, 'Cф', '1 µF', side='l') + gnd(nx, 340)
    s += L(nx, ny, 880, ny) + dot(720, ny)
    s += res_v(720, ny, 290, 'Rогр', '100 Ω', 'r', w=12, h=28) + button_v(720, 290, 360, 'SB1') + gnd(720, 360)
    s += ring(880, ny, 4) + T(880, ny - 12, 'D5', 10.5, True)
    s += T(760, 170, 'τ = Rп · Cф = 10 ms', 10.5, an='start', it=True)
    s += caption(W, 424, 'Фиг. 2.3. Свързване на бутоните: а) прието решение; б) алтернатива с RC филтър')
    write('fig_2_3_butoni', W, H, s)


# ============================================================================
#  Фиг. 2.4 - времедиаграма на четене на DHT11
# ============================================================================
def fig_2_4():
    W, H = 1120, 420
    s = ''
    hi, lo = 150, 220
    seg = [(60, 170, hi, 1.8), (170, 390, lo, 3.2), (390, 430, hi, 1.8), (430, 510, lo, 1.8),
           (510, 590, hi, 1.8), (590, 640, lo, 1.8), (640, 670, hi, 1.8), (670, 720, lo, 1.8),
           (720, 800, hi, 1.8), (880, 930, lo, 1.8), (930, 1060, hi, 1.8)]
    prev = None
    for x1, x2, y, w in seg:
        if prev is not None and prev[1] == x1:
            s += L(x1, prev[2], x1, y, 1.8)
        s += L(x1, y, x2, y, w)
        prev = (x1, x2, y)
    s += L(800, hi, 815, hi, 1.8) + L(815, hi, 815, lo, 1.8) + L(815, lo, 865, lo, 1.8, '5 4')
    s += L(865, lo, 865, hi, 1.8, '5 4') + L(865, hi, 880, hi, 1.8, '5 4') + L(880, hi, 880, lo, 1.8)
    s += T(840, hi - 8, '…', 14, True)
    s += T(48, hi + 4, '1', 11, True, 'end') + T(48, lo + 4, '0', 11, True, 'end')

    def dim(x1, x2, y, lbl, fs=9.5):
        return (L(x1, y - 5, x1, y + 5, 1) + L(x2, y - 5, x2, y + 5, 1) +
                L(x1 + 1, y, x2 - 1, y, 1, end=True, start=True) + T((x1 + x2) / 2, y + 16, lbl, fs))
    s += dim(110, 170, 250, '1 ms') + dim(170, 390, 250, '≥ 18 ms (в библиотеката 20 ms)')
    s += dim(390, 430, 280, '20…40 µs') + dim(430, 510, 250, '80 µs') + dim(510, 590, 250, '80 µs')
    s += dim(590, 640, 280, '50 µs') + dim(640, 670, 250, '26…28', 9) + dim(670, 720, 280, '50 µs')
    s += dim(720, 800, 250, '70 µs') + dim(880, 930, 280, '50 µs')

    def brace(x1, x2, y, lbl, fs=10.5, b=True):
        return (L(x1, y, x1, y + 8, 1.2) + L(x2, y, x2, y + 8, 1.2) + L(x1, y, x2, y, 1.2) +
                T((x1 + x2) / 2, y - 6, lbl, fs, b))
    s += brace(110, 430, 100, 'управлява микроконтролерът') + brace(430, 930, 100, 'управлява датчикът')
    s += brace(590, 670, 128, 'бит „0“', 10, False) + brace(670, 800, 128, 'бит „1“', 10, False)
    s += T(835, 124, '40 бита = 5 байта', 10, it=True)
    s += R(110, 316, 280, 26, 1, '#eef2f5') + T(250, 334, 'delay() – Wi-Fi стекът работи', 10)
    s += R(430, 316, 500, 26, 1, '#f4e9e9') + T(680, 334, 'прекъсванията са забранени (≈ 3…5 ms)', 10)
    s += T(1060, 176, 'покой', 10, an='end', it=True) + T(60, 176, 'покой', 10, an='start', it=True)
    s += T(560, 376, 'Общо едно четене: ≈ 24…26 ms, блокиращо за главния цикъл на програмата', 10.5, True)
    s += caption(W, 406, 'Фиг. 2.4. Времедиаграма на едно четене на DHT11 (мащабът не е спазен)')
    s = f'<g transform="translate(0,-62)">{s}</g>'
    write('fig_2_4_dht11_protokol', W, H - 62, s)


# ============================================================================
#  Фиг. 2.5 - типична схема на модула HR202 с LM393
# ============================================================================
def fig_2_5():
    W, H = 940, 520
    s = ''
    yv, yg = 80, 430
    s += L(100, yv, 820, yv, 2) + L(100, yg, 820, yg, 2)
    s += T(96, yv + 4, '+3,3 V', 11, True, 'end') + T(96, yg + 4, 'GND', 11, True, 'end')
    # HR202 и R1 - делител
    ax, ay = 170, 245
    s += res_v(ax, yv, ay, 'B1', 'HR202', 'l', w=16, h=44) + dot(ax, yv)
    s += T(ax - 14, (yv + ay) / 2 + 26, 'RH ↑ → R ↓', 10, an='end')
    s += res_v(ax, ay, yg, 'R1', '10 kΩ', 'l') + dot(ax, yg) + dot(ax, ay) + T(ax + 8, ay - 8, 'UA', 10, True, 'start')
    # потенциометър RP
    px = 300
    s += L(px, yv, px, 290) + R(px - 7, 290, 14, 90, 1.5) + L(px, 380, px, yg) + dot(px, yv) + dot(px, yg)
    s += T(px - 14, 330, 'RP', 11, True, 'end') + T(px - 14, 344, '10 kΩ', 10, an='end')
    s += L(px + 32, 335, px + 10, 335, 1.4, end=True) + T(px + 40, 328, 'Uпраг', 10, True, 'start')
    # компаратор
    s += (f'<polygon points="430,200 430,330 540,265" fill="#fff" stroke="#000" stroke-width="1.6"/>')
    s += T(440, 236, '−', 13, True, 'start') + T(440, 304, '+', 13, True, 'start')
    s += T(485, 186, 'DA1 (LM393)', 11, True)
    s += L(ax, ay, 400, ay) + L(400, ay, 400, 232) + L(400, 232, 430, 232)
    s += P([(px + 32, 335), (380, 335), (380, 298), (430, 298)])
    # изход с отворен колектор, R2 към +3,3 V, светодиод HL1
    ox = 610
    s += L(540, 265, ox, 265) + dot(ox, 265)
    s += res_v(ox, yv, 265, 'R2', '10 kΩ', 'l') + dot(ox, yv)
    s += L(ox, 265, 700, 265) + dot(700, 265)
    s += res_v(700, yv, 170, 'R3', '1 kΩ', 'r', w=10, h=26) + dot(700, yv)
    s += led_v(700, 170, 265, 'HL1')
    s += L(700, 265, 820, 265) + ring(824, 265, 5) + T(836, 262, 'DO', 11, True, 'start')
    s += T(836, 278, '→ D7 (GPIO13)', 10, an='start')
    s += T(520, 360, 'сухо: UA < Uпраг → DO = 1', 10.5, it=True)
    s += T(520, 378, 'влага: UA > Uпраг → DO = 0 (светва HL1)', 10.5, it=True)
    s += T(100, 470, 'Модулът е захранен от 3,3 V на NodeMCU, затова DO е в границите 0…3,3 V (без преобразуване на нивото).', 10, an='start')
    s += caption(W, 504, 'Фиг. 2.5. Типична схема на датчика за влага HR202 (модул с LM393)')
    write('fig_2_5_hr202_modul', W, H, s)


# ============================================================================
#  Фиг. 2.6 - канал за измерване на захранващото напрежение
# ============================================================================
def fig_2_6():
    W, H = 1000, 560
    s = ''
    x0 = 150
    s += supply(x0, 70, '+5 V (VIN)') + res_v(x0, 70, 215, 'R1', '47 kΩ', 'l')
    s += dot(x0, 225) + L(x0, 215, x0, 235) + res_v(x0, 235, 360, 'R2', '20 kΩ', 'l')
    s += L(x0, 225, 440, 225) + ring(440, 225, 4)
    s += T(300, 215, 'UA0 = k1 · UVIN', 10.5, it=True)
    # общ път на масата
    s += dot(x0, 375) + L(x0, 360, x0, 375)
    s += res_h(x0, 440, 375, 'Rм', 'път на масата (≈ 0,3 Ω, оценка)') + ring(440, 375, 4)
    s += P([(40, 330), (95, 330), (95, 375), (x0, 375)], 1.5, end=True)
    s += T(40, 322, 'Σ IK от релетата', 10, an='start', it=True)
    s += L(165, 405, 425, 405, 1, end=True, start=True) + T(295, 422, 'Ug', 11, True)
    # NodeMCU
    s += R(440, 60, 520, 390, 1.4, '#fafafa', '6 4', 4) + T(700, 82, 'NodeMCU v3', 11, True)
    s += T(452, 218, 'A0', 11, True, 'start') + T(452, 368, 'GND', 11, True, 'start')
    s += res_h(440, 620, 225, '', '') + T(530, 212, '220 kΩ', 10)
    tx = 640
    s += L(620, 225, tx, 225) + dot(tx, 225) + res_v(tx, 225, 375, '', '', w=12, h=34)
    s += T(tx + 14, 296, '100 kΩ', 10, an='start') + L(440, 375, tx, 375) + dot(tx, 375)
    s += T(tx - 6, 214, 'TOUT', 10, True, 'end')
    s += L(tx, 225, 740, 225, end=True)
    s += R(740, 175, 200, 100, 1.5, '#eef2f5') + T(840, 206, 'ESP8266EX', 11, True)
    s += T(840, 226, 'АЦП 10 бита', 10) + T(840, 244, '0…1,0 V → 0…1023', 10)
    s += T(700, 330, 'k2 = 100 / 320 = 0,3125', 10.5, it=True, an='start')
    s += T(40, 470, 'k1 = R2′ / (R1 + R2′) = 0,2860, където R2′ = R2 ∥ 320 kΩ = 18,82 kΩ', 10.5, an='start')
    s += T(40, 490, 'Пълна скала: 1,0 V / (k1 · k2) = 11,19 V (изчислена); 10,91 V (калибрирана с мултиметър)', 10.5, an='start')
    s += T(40, 510, 'Изместване на масата: Uизч = UVIN + 2,5 · Ug – обяснява грешката от ≈ 55 mV на включено реле', 10.5, an='start')
    s += caption(W, 546, 'Фиг. 2.6. Канал за измерване на захранващото напрежение')
    write('fig_2_6_izmervane_vcc', W, H, s)


# ============================================================================
#  Фиг. 2.7 - принципна схема на подчинения възел
# ============================================================================
def net(x, y, name, side='r', stub=26):
    """Означение на верига: изводна линия и надпис в рамка."""
    w = 12 + 8 * len(name)
    if side == 'r':
        return L(x, y, x + stub, y) + R(x + stub, y - 10, w, 20, 1.2) + T(x + stub + w / 2, y + 4, name, 10.5, True)
    return L(x - stub, y, x, y) + R(x - stub - w, y - 10, w, 20, 1.2) + T(x - stub - w / 2, y + 4, name, 10.5, True)


def fig_2_7():
    W, H = 1300, 920
    s = ''
    # DD1 - NodeMCU
    bx, by, bw, bh = 560, 110, 230, 540
    s += R(bx, by, bw, bh, 2) + T(bx + bw / 2, by + 30, 'DD1', 12, True)
    s += T(bx + bw / 2, by + 48, 'NodeMCU v3', 11, True) + T(bx + bw / 2, by + 64, '(ESP8266EX)', 10)
    s += R(bx + 80, by - 30, 70, 30, 1.5) + T(bx + 115, by - 11, 'USB', 10, True)
    s += T(bx + 115, by - 40, 'XS1 – 5 V от адаптер', 10, it=True)
    left = [('VIN', 200), ('GND', 260), ('3V3', 320), ('A0', 380), ('RX', 480), ('TX', 520)]
    right = [('D0', 200), ('D1', 250), ('D2', 300), ('D8', 350), ('D4', 420), ('D7', 470), ('D5', 540), ('D6', 590)]
    gpio = {'D0': 'GPIO16', 'D1': 'GPIO5', 'D2': 'GPIO4', 'D8': 'GPIO15', 'D4': 'GPIO2',
            'D7': 'GPIO13', 'D5': 'GPIO14', 'D6': 'GPIO12'}
    for n, yy in left:
        s += T(bx + 8, yy + 4, n, 10.5, True, 'start')
    for n, yy in right:
        s += T(bx + bw - 8, yy + 4, f'{n} ({gpio[n]})', 10, True, 'end')
    s += net(bx, 200, '+5 V', 'l') + L(bx - 30, 260, bx, 260) + gnd(bx - 30, 260)
    s += net(bx, 320, '+3,3 V', 'l') + net(bx, 380, 'A0', 'l')
    s += L(bx - 40, 480, bx, 480) + L(bx - 40, 520, bx, 520)
    s += T(bx - 46, 496, 'UART – свободни', 10, an='end', it=True) + T(bx - 46, 512, 'за конфигуриране', 10, an='end', it=True)
    for n, yy in right:
        s += net(bx + bw, yy, n, 'r')
    # делител R1-R2 и C1
    x1 = 120
    s += net(x1, 140, '+5 V', 'l', 0)
    s += L(x1, 140, x1, 160) + res_v(x1, 160, 260, 'R1', '47 kΩ', 'r') + dot(x1, 275) + L(x1, 260, x1, 290)
    s += res_v(x1, 290, 400, 'R2', '20 kΩ', 'r') + L(x1, 275, 200, 275) + net(200, 275, 'A0', 'r', 0)
    s += gnd(x1, 400) + T(x1, 438, 'към GND на DD1', 9.5, it=True) + T(x1, 451, 'с отделен проводник', 9.5, it=True)
    x2 = 330
    s += net(x2, 140, '+5 V', 'l', 0) + L(x2, 140, x2, 220) + cap_v(x2, 220, 340, 'C1', '1000 µF / 16 V', True, 'r') + gnd(x2, 340)
    # релейни модули A1...A4
    mx, mw, mh = 950, 190, 112
    s += T(mx + mw + 44, 100, 'товари ~230 V', 10, True)
    for i, (pin, yy) in enumerate((('D0', 110), ('D1', 240), ('D2', 370), ('D8', 500))):
        s += R(mx, yy, mw, mh, 1.6) + T(mx + mw / 2, yy + 20, f'A{i + 1} – релеен модул', 10.5, True)
        s += T(mx + mw / 2, yy + 36, 'джъмпер H', 9.5, it=True)
        for k, (lbl, net_name) in enumerate((('VCC', '+5 V'), ('IN', pin), ('GND', 'GND'))):
            py = yy + 52 + k * 24
            s += T(mx + 8, py + 4, lbl, 9.5, an='start') + net(mx, py, net_name, 'l', 14)
        for k, lbl in enumerate(('NO', 'COM', 'NC')):
            py = yy + 52 + k * 24
            s += T(mx + mw - 8, py + 4, lbl, 9.5, an='end') + L(mx + mw, py, mx + mw + 16, py) + ring(mx + mw + 20, py, 4)
        s += T(mx + mw + 32, yy + 80, f'товар {i + 1}', 9.5, an='start', it=True)
    # датчици и бутони
    s += R(430, 720, 170, 110, 1.6) + T(515, 738, 'BK1 – DHT11', 10.5, True)
    s += T(515, 753, 'температура и влажност', 9.5, it=True)
    for k, (lbl, nn) in enumerate((('VCC', '+3,3 V'), ('DATA', 'D4'), ('GND', 'GND'))):
        py = 772 + k * 22
        s += T(438, py + 4, lbl, 9.5, an='start') + net(430, py, nn, 'l', 14)
    s += R(660, 720, 190, 110, 1.6) + T(755, 738, 'A5 – HR202 + LM393', 10.5, True)
    s += T(755, 753, 'датчик за влага', 9.5, it=True)
    for k, (lbl, nn) in enumerate((('VCC', '+3,3 V'), ('DO', 'D7'), ('GND', 'GND'))):
        py = 772 + k * 22
        s += T(842, py + 4, lbl, 9.5, an='end') + net(850, py, nn, 'r', 14)
    for xk, nn, lbl in ((1080, 'D5', 'SB1'), (1190, 'D6', 'SB2')):
        s += net(xk, 710, nn, 'l', 0) + L(xk, 710, xk, 720) + button_v(xk, 720, 810, lbl) + gnd(xk, 810)
    s += T(60, 868, 'Всички релейни модули са с джъмпер в положение H. Връзките са означени с имената на веригите.', 10, an='start')
    s += T(60, 884, 'Контактите NO, COM, NC се свързват към товарите само чрез клемите на модулите, извън макетната платка.', 10, an='start')
    s += caption(W, 912, 'Фиг. 2.7. Принципна схема на подчинения възел')
    write('fig_2_7_principna_shema', W, H, s)


# ============================================================================
#  Фиг. 2.8 - снимка на макета
# ============================================================================
def fig_2_8():
    img = open(OUT + 'foto/maket.png', 'rb').read()
    b64 = base64.b64encode(img).decode('ascii')
    iw, ih = 480, 640
    W, H = iw + 40, ih + 70
    s = (f'<image x="20" y="16" width="{iw}" height="{ih}" preserveAspectRatio="xMidYMid meet" '
         f'xlink:href="data:image/png;base64,{b64}"/>' + R(20, 16, iw, ih, 1.2, 'none'))
    s += caption(W, ih + 50, 'Фиг. 2.8. Макет на подчинения възел')
    write('fig_2_8_maket', W, H, s)


if __name__ == '__main__':
    os.makedirs(OUT + 'png', exist_ok=True)
    for f in (fig_2_1, fig_2_2, fig_2_3, fig_2_4, fig_2_5, fig_2_6, fig_2_7, fig_2_8):
        f()

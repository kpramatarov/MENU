#!/usr/bin/env python3
"""Генерира фигурите на Глава 4 (SVG + PNG).
Употреба:  python3 figuri/generirane_figuri_gl4.py   (от папката diplomna_rabota)
Използва примитивите от generirane_figuri_gl2.py и generirane_figuri_gl3.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generirane_figuri_gl2 import L, P, R, T, dot, res_v, write  # noqa: E402
from generirane_figuri_gl3 import (W, SM, TS, _warn, arrow, caption, check,  # noqa: E402
                                   label_lines)


def box(x, y, w, h, title, lines=(), fill='#fff', dash=None, fs=12):
    """Правоъгълник със заглавие и редове текст, центрирани."""
    check([title], w - 12, 12.5, True, where='кутия')
    check(lines, w - 12, fs, where='кутия')
    n = len(lines)
    s = R(x, y, w, h, 1.5, fill, dash, 4)
    y0 = y + h / 2 - (n * 16) / 2 + 4
    s += T(x + w / 2, y0, title, 12.5, True)
    for k, t in enumerate(lines):
        s += T(x + w / 2, y0 + 17 + k * 16, t, fs)
    return s


def meter(cx, cy, sym, r=11):
    return (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#fff" stroke="#000" stroke-width="1.5"/>' +
            T(cx, cy + 4.5, sym, 12.5, True))


# ============================================================================
#  Фиг. 4.1 - опитна постановка и точки на измерване
# ============================================================================
def fig_4_1():
    H = 640
    s = ''
    # --- горна част: устройства и връзки
    s += box(20, 24, 210, 96, 'Персонален компютър', ['PuTTY: запис на UART', 'във файл (LOG ON, PING)'])
    s += box(300, 24, 180, 96, 'Главен възел', ['NodeMCU v3', 'Master.ino, версия 3'])
    s += box(570, 24, 190, 96, 'Подчинен възел', ['NodeMCU v3', 'Slave.ino, версия 3'])
    s += L(230, 72, 300, 72, 1.6, end=True, start=True) + T(265, 64, 'USB', SM) + T(265, 89, 'UART, 5 V', SM)
    s += L(480, 72, 570, 72, 1.6, '8 4', True, True) + T(525, 64, 'ESP-NOW', SM, True) + T(525, 89, '1…2 m', SM)
    s += box(20, 160, 210, 56, 'Уеб клиенти', ['смартфон, лаптоп (1…3)'])
    s += box(300, 160, 180, 56, 'Wi-Fi рутер', ['фиксиран канал'])
    s += L(230, 188, 300, 188, 1.5, '8 4', True, True) + T(265, 180, '802.11', SM)
    s += L(390, 160, 390, 120, 1.5, '8 4', True, True) + T(398, 145, 'HTTP', SM, an='start')
    s += box(570, 160, 190, 56, 'Адаптер 5 V / 1 A')
    s += arrow((665, 160), (665, 120)) + T(673, 145, 'USB', SM, an='start')

    # --- долна част: точки на измерване в подчинения възел
    x0, y0 = 150, 256
    s += R(x0, y0, 620, 320, 1.2, '#fff', '7 4', 6)
    s += T(x0 + 12, y0 + 22, 'Точки на измерване в подчинения възел (опити Е3 и Е5)', 12.5, True, 'start')
    s += L(665, 216, 665, y0, 1.2, '3 3')
    # NodeMCU: VIN и GND вляво, A0 вдясно
    nx, ny, nw, nh = 330, 330, 100, 150
    s += R(nx, ny, nw, nh, 1.6, '#eef2f5') + T(nx + nw / 2, ny + 100, 'NodeMCU', 12.5, True)
    s += T(nx + nw / 2, ny + 117, 'v3', 11.5)
    yv, yg, ya = ny + 22, ny + nh - 22, ny + 62
    s += L(nx - 20, yv, nx, yv, 1.5) + T(nx + 5, yv + 4, 'VIN', 11, an='start')
    s += L(nx - 20, yg, nx, yg, 1.5) + T(nx + 5, yg + 4, 'GND', 11, an='start')
    s += L(nx + nw, ya, nx + nw + 20, ya, 1.5) + T(nx + nw - 5, ya + 4, 'A0', 11, an='end')
    # шина +5 V над платката
    yb, ygb = 300, 530
    s += P([(nx - 20, yv), (nx - 20, yb), (755, yb)], 1.8) + T(560, yb - 7, '+5 V (VIN)', 11.5, True)
    # маса: извод GND -> R_g -> маса на макетната платка
    s += P([(nx - 20, yg), (nx - 20, ygb), (372, ygb)], 1.8)
    s += R(372, ygb - 7, 40, 14, 1.3, '#fff', '3 2') + TS(392, ygb - 13, 'R', 'g', 12)
    s += L(412, ygb, 755, ygb, 1.8) + T(640, ygb + 17, 'маса на макетната платка', 11.5)
    # V1 - напрежение на шината
    xv1 = nx - 50
    s += P([(nx - 20, yv), (xv1, yv), (xv1, 394)], 1.3) + meter(xv1, 405, 'V')
    s += P([(xv1, 416), (xv1, yg), (nx - 20, yg)], 1.3)
    s += dot(nx - 20, yv, 2.8) + dot(nx - 20, yg, 2.8)
    s += T(xv1 - 16, 401, 'V1', 12, True, 'end') + TS(xv1 - 16, 420, 'U', 'VIN', 12, an='end')
    # делител R1 / R2
    xr = 520
    s += res_v(xr, yb, ya, 'R1', '47 kΩ') + dot(xr, yb, 3)
    s += L(nx + nw + 20, ya, xr, ya, 1.5) + dot(xr, ya, 3)
    s += res_v(xr, ya, ygb, 'R2', '20 kΩ') + dot(xr, ygb, 3)
    # V2 - изместване на масата
    s += P([(350, ygb), (350, 556), (429, 556)], 1.3) + dot(350, ygb, 2.8) + meter(440, 556, 'V')
    s += P([(451, 556), (xr, 556), (xr, ygb)], 1.3)
    s += T(472, 551, 'V2', 12, True, 'start') + TS(472 + 24, 551, 'U', 'g', 12, an='start')
    # релейни модули с амперметър
    xm = 690
    s += L(xm, yb, xm, 322, 1.5) + dot(xm, yb, 3) + meter(xm, 333, 'A') + L(xm, 344, xm, 380, 1.5)
    s += R(xm - 55, 380, 110, 70, 1.5, '#fff', rx=3) + T(xm, 410, 'Релейни', 12, True) + T(xm, 427, 'модули × 4', 12)
    s += L(xm, 450, xm, ygb, 1.5) + dot(xm, ygb, 3)
    # означения
    s += label_lines(20, 598, ['V1 – напрежение на шината +5 V; V2 – изместване на масата между долния край на R2 и извода GND;',
                               'A – ток на релейните модули; прекъснатият правоъгълник – съпротивление на общия път на масата.'],
                     fs=11.5, lh=16)
    check(['V1 – напрежение на шината +5 V; V2 – изместване на масата между долния край на R2 и извода GND;'],
          740, 11.5, where='означения')
    s += caption(H + 30, 'Фиг. 4.1. Опитна постановка и точки на измерване')
    write('fig_4_1_postanovka', W, H + 30, s)


# ============================================================================
#  Фиг. 4.2 - времедиаграма на един обмен при теста PING (1 Mbit/s)
# ============================================================================
def fig_4_2():
    H = 430
    s = ''
    lanes = (('Главен възел', 80), ('Радиоканал', 170), ('Подчинен възел', 262))
    for name, y in lanes:
        s += T(16, y + 5, name, 12.5, True, 'start') + L(130, y, 768, y, 1.0, '2 4')

    def seg(x1, x2, y, l1='', l2='', fill='#fff', h=34):
        r = R(x1, y - h / 2, x2 - x1, h, 1.3, fill)
        if l1 and l2:
            r += T((x1 + x2) / 2, y - 2, l1, 11.5) + T((x1 + x2) / 2, y + 13, l2, 10.5)
        elif l1:
            r += T((x1 + x2) / 2, y + 4, l1, 11.5)
        return r

    ym, yr, ys = 80, 170, 262
    # команда: DIFS, случайно изчакване, кадър, SIFS + ACK
    s += seg(140, 162, yr, fill='#f0f0f0') + seg(162, 200, yr, fill='#dcdcdc')
    s += T(151, yr + 33, 'DIFS', 10.5) + label_lines(190, yr + 47, ['изчакване', '0…620 µs'], fs=10.5, an='middle', lh=13)
    s += seg(200, 300, yr, 'команда', '696 µs')
    s += seg(305, 355, yr, 'ACK', '304 µs')
    s += dot(140, ym, 3.2) + arrow((140, ym), (140, yr - 18)) + T(140, ym - 10, 'esp_now_send()', SM)
    s += arrow((355, yr - 18), (355, ym)) + dot(355, ym, 3.2) + T(362, ym - 10, 'OnDataSent()', SM, an='start')
    # подчинен възел
    s += arrow((300, yr + 18), (300, ys - 17)) + T(307, yr + 56, 'OnDataRecv()', SM, an='start')
    s += seg(300, 470, ys, 'изчакване на итерацията', fill='#e8e8e8')
    s += label_lines(385, ys + 33, ['обикновено < 0,1 ms;', 'до 25 ms при четене на DHT11'], fs=10.5, an='middle', lh=13)
    s += seg(470, 525, ys, 'loop()')
    # отчет
    s += arrow((525, ys - 17), (525, yr + 18))
    s += seg(525, 560, yr, fill='#dcdcdc') + label_lines(542, yr - 44, ['DIFS +', 'изчакване'], fs=10.5, an='middle', lh=13)
    s += seg(560, 710, yr, 'отчет (144 B)', '1688 µs')
    s += seg(715, 765, yr, 'ACK', '304 µs')
    s += arrow((710, yr - 18), (710, ym)) + dot(710, ym, 3.2) + T(703, ym - 10, 'OnDataRecv()', SM, an='end')
    # размерни линии
    yd = 32
    s += L(140, yd, 355, yd, 1.2, end=True, start=True) + L(140, yd - 6, 140, ym - 22, 0.8, '2 2') + L(355, yd - 6, 355, ym - 22, 0.8, '2 2')
    s += TS(247, yd - 6, 't', 'потв', 12.5) + T(247, yd + 16, '1,06…1,68 ms', SM)
    y2 = 340
    s += L(140, y2, 710, y2, 1.2, end=True, start=True) + L(140, ym + 4, 140, y2 + 6, 0.8, '2 2') + L(710, ym + 4, 710, y2 + 6, 0.8, '2 2')
    s += T(425, y2 - 7, 'RTT ≈ 2,8…4,0 ms; до ≈ 29 ms при съвпадение с четенето на DHT11', 12)
    s += label_lines(390, 372, ['Времената са изчислени за 1 Mbit/s (802.11b); мащабът по времето не е спазен.'],
                     an='middle', it=True)
    s += caption(H, 'Фиг. 4.2. Времедиаграма на един обмен при теста PING')
    write('fig_4_2_ping', W, H, s)


if __name__ == '__main__':
    os.makedirs('figuri/png', exist_ok=True)
    for f in (fig_4_1, fig_4_2):
        f()
    for w in _warn:
        print('  ! не се побира:', w)

#!/usr/bin/env python3
"""Генерира фигурите на Глава 4 (SVG + PNG).
Употреба:  python3 figuri/generirane_figuri_gl4.py   (от папката diplomna_rabota)
Използва примитивите от generirane_figuri_gl2.py и generirane_figuri_gl3.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generirane_figuri_gl2 import L, P, R, T, dot, res_v, write  # noqa: E402
from generirane_figuri_gl3 import (W, SM, TR, TS, _warn, arrow, caption, check,  # noqa: E402
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
#  Фиг. 4.1 - структура на симулационния модел
# ============================================================================
def fig_4_1():
    H = 624
    s = ''
    # потребители
    s += box(20, 18, 740, 64, 'Въздействия („потребители“)',
             ['браузър (/data всяка секунда, команди), тест PING, бутони с трептене, намокряне,',
              'грешки на DHT11, рестарти, прекъсвания на връзката'], '#f4f4f4', fs=11.5)
    # двата възела
    s += box(20, 130, 300, 150, 'Подчинен възел',
             ['Slave.ino – без промени', 'имитация на хардуера: изводи,', 'АЦП, DHT11, EEPROM, UART,',
              'собствен часовник (µs)'])
    s += box(460, 130, 300, 150, 'Главен възел',
             ['Master.ino – без промени', 'имитация: уеб сървър, UART,', 'EEPROM, собствен часовник (µs)',
              'рестарт = ново зареждане'])
    s += arrow((170, 82), (170, 130)) + T(178, 110, 'изводи', SM, an='start')
    s += arrow((610, 82), (610, 130)) + T(618, 110, 'HTTP, UART', SM, an='start')
    # канал
    s += box(170, 330, 440, 96, 'Модел на радиоканала (IEEE 802.11b, 1 Mbit/s)',
             ['DIFS, случайно изчакване с удвояване, до 7 опита, ACK;',
              'грешки с вероятност FER, дублиране, фонов трафик, прекъсвания'], fs=11.5)
    s += P([(170, 280), (170, 305), (330, 305), (330, 330)], 1.5, '8 4', end=True, start=True)
    s += P([(610, 280), (610, 305), (450, 305), (450, 330)], 1.5, '8 4', end=True, start=True)
    s += T(250, 298, 'отчети', SM) + T(530, 298, 'команди', SM)
    # проверки и резултати
    s += box(20, 470, 440, 96, 'Проверки по време на симулацията',
             ['защита при течност, съгласуваност на състоянието,', 'отчитане на загубите, напредък на опашката,',
              'период на отчетите'], fs=11.5)
    s += box(500, 470, 260, 96, 'Резултати',
             ['обобщение (JSON), закъснения (CSV),', 'записи по UART на двата възела'], fs=11.5)
    s += arrow((240, 426), (240, 470)) + arrow((460, 518), (500, 518))
    s += L(20, 205, 12, 205, 1.2) + L(12, 205, 12, 518, 1.2) + arrow((12, 518), (20, 518))
    s += L(760, 205, 772, 205, 1.2) + L(772, 205, 772, 452, 1.2) + L(772, 452, 240, 452, 1.2)
    s += caption(H, 'Фиг. 4.1. Структура на симулационния модел')
    write('fig_4_1_simulacia', W, H, s)


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


# ============================================================================
#  Фиг. 4.3 - разпределение на двупосочното закъснение (симулация)
# ============================================================================
def _rtt(name):
    import csv
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'firmware', 'simulacia',
                        'rezultati', name + '_rtt.csv')
    return [float(r['rtt_us']) / 1000 for r in csv.DictReader(open(path))]


def fig_4_3():
    import math
    H = 650
    s = ''
    x0, x1 = 90, 750                       # ос на времето: 0...55 ms
    xmax = 55.0
    X = lambda v: x0 + (x1 - x0) * v / xmax
    lo, hi = -2, 2                          # 0,01 % ... 100 %
    panels = [('rtt', 'а) Идеален канал', 40), ('rtt_bg', 'б) Зает канал: FER 20 %, 30 % фонов трафик', 320)]
    for name, title, top in panels:
        v = _rtt(name)
        n = len(v)
        ph = 190
        Y = lambda p: top + 34 + ph * (hi - math.log10(p)) / (hi - lo)
        ybot = top + 34 + ph
        mean = sum(v) / n
        over = 100.0 * sum(1 for x in v if x > 10) / n
        s += T(x0, top + 12, title, 12.5, True, 'start')
        s += T(x0, top + 28, f'{n} заявки; средно {mean:.2f} ms; над 10 ms – {over:.2f} %'.replace('.', ','), 11.5, an='start')
        if name == 'rtt':                   # изчисленият интервал
            s += R(X(2.8), top + 34, X(4.0) - X(2.8), ph, 0, '#e6e6e6').replace('stroke="#000"', 'stroke="none"')
            s += T(X(4.0) + 4, top + 50, 'изчислено 2,8…4,0 ms', 11, an='start')
            s += L(X(29), top + 34, X(29), ybot, 1.0, '4 3') + T(X(29) + 4, top + 50, '≈ 29 ms', 11, an='start')
        for e in range(lo, hi + 1):         # решетка и деления по оста y
            y = Y(10 ** e)
            s += L(x0, y, x1, y, 0.6, '2 3') if e > lo else ''
            lbl = {-2: '0,01', -1: '0,1', 0: '1', 1: '10', 2: '100'}[e]
            s += T(x0 - 8, y + 4, lbl, 11, an='end')
        bins = [0] * int(xmax)
        for x in v:
            b = int(x)
            if b < len(bins):
                bins[b] += 1
        for b, c in enumerate(bins):        # стълбове с междина 2 px
            if c == 0:
                continue
            p = 100.0 * c / n
            y = Y(max(p, 10 ** lo))
            s += f'<rect x="{X(b) + 1:.1f}" y="{y:.1f}" width="{X(b + 1) - X(b) - 2:.1f}" height="{ybot - y:.1f}" fill="#555"/>'
        s += L(x0, ybot, x1, ybot, 1.2) + L(x0, top + 34, x0, ybot, 1.2)
        for t in range(0, 56, 5):
            s += L(X(t), ybot, X(t), ybot + 5, 1.0) + T(X(t), ybot + 18, str(t), 11)
        s += TR(x0 - 52, top + 34 + ph / 2, 'Дял от заявките, %', 11.5)
    s += T((x0 + x1) / 2, 600, 'Двупосочно закъснение RTT, ms (стълб – интервал от 1 ms; логаритмичен мащаб по вертикалата)', 11.5)
    s += caption(H, 'Фиг. 4.3. Разпределение на закъснението при теста PING (симулация)')
    write('fig_4_3_rtt_histogram', W, H, s)


if __name__ == '__main__':
    os.makedirs('figuri/png', exist_ok=True)
    for f in (fig_4_1, fig_4_2, fig_4_3):
        f()
    for w in _warn:
        print('  ! не се побира:', w)

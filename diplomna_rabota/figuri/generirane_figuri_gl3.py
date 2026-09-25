#!/usr/bin/env python3
"""Генерира фигурите на Глава 3 (SVG + PNG).
Употреба:  python3 figuri/generirane_figuri_gl3.py   (от папката diplomna_rabota)
Изисква:   cairosvg, Pillow (за проверка дали текстът се побира в блоковете)

Фигурите са с ширина 780 px и се вмъкват с ширина 16 cm, т. е. шрифт 12,5 px
съответства на около 7 pt при печат.
"""
import os
import sys

from PIL import ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generirane_figuri_gl2 import L, P, R, T, dot, write  # noqa: E402

W = 780
FS = 12.5          # основен шрифт
SM = 11.5          # надписи по стрелките
MONO = 'DejaVu Sans Mono, Liberation Mono, monospace'
_FONTS = {}
_warn = []


def _font(b, mono):
    k = (b, mono)
    if k not in _FONTS:
        name = 'DejaVuSansMono.ttf' if mono else ('DejaVuSans-Bold.ttf' if b else 'DejaVuSans.ttf')
        _FONTS[k] = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/' + name, 100)
    return _FONTS[k]


def tw(s, fs=FS, b=False, mono=False):
    """Ширина на текста в пиксели (DejaVu Sans, както при изобразяването)."""
    return _font(b, mono).getlength(s) * fs / 100


def check(lines, w, fs=FS, b=False, mono=False, where=''):
    for s in lines:
        if tw(s, fs, b, mono) > w:
            _warn.append(f'{where}: „{s}" {tw(s, fs, b, mono):.0f} px > {w:.0f} px')


def TM(x, y, s, fs=FS, an='start', b=False):
    s = str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    return (f'<text x="{x}" y="{y}" font-family="{MONO}" font-size="{fs}" '
            f'font-weight="{"bold" if b else "normal"}" text-anchor="{an}">{s}</text>')


def lines_at(cx, cy, lines, fs=FS, b=False, lh=None):
    lh = lh or fs * 1.32
    y0 = cy - (len(lines) - 1) * lh / 2 + fs * 0.36
    return ''.join(T(cx, y0 + i * lh, s, fs, b) for i, s in enumerate(lines))


def caption(H, s):
    return T(W / 2, H - 16, s, 15, True)


# ------------------------------------------------------------ блокове на блок-схема
class B:
    """Блок: cx - център по x, y - горен край, w, h - размери."""

    def __init__(self, kind, cx, y, w, h, lines, fs=FS, b=False):
        self.k, self.cx, self.y, self.w, self.h = kind, cx, y, w, h
        self.lines, self.fs, self.b = lines, fs, b
        room = {'dec': w * 0.62, 'io': w - 40, 'sub': w - 34}.get(kind, w - 18)
        check(lines, room, fs, b, where=kind)

    top = property(lambda s: (s.cx, s.y))
    bot = property(lambda s: (s.cx, s.y + s.h))
    lft = property(lambda s: (s.cx - s.w / 2, s.y + s.h / 2))
    rgt = property(lambda s: (s.cx + s.w / 2, s.y + s.h / 2))
    my = property(lambda s: s.y + s.h / 2)

    def svg(self):
        x0, x1, y0, y1, cx = self.cx - self.w / 2, self.cx + self.w / 2, self.y, self.y + self.h, self.cx
        k = self.k
        if k == 'term':
            s = R(x0, y0, self.w, self.h, 1.6, '#fff', rx=self.h / 2)
        elif k == 'proc':
            s = R(x0, y0, self.w, self.h, 1.5)
        elif k == 'sub':
            s = R(x0, y0, self.w, self.h, 1.5) + L(x0 + 9, y0, x0 + 9, y1, 1.2) + L(x1 - 9, y0, x1 - 9, y1, 1.2)
        elif k == 'dec':
            s = (f'<polygon points="{cx},{y0} {x1},{(y0 + y1) / 2} {cx},{y1} {x0},{(y0 + y1) / 2}" '
                 f'fill="#fff" stroke="#000" stroke-width="1.5"/>')
        elif k == 'io':
            d = 14
            s = (f'<polygon points="{x0 + d},{y0} {x1},{y0} {x1 - d},{y1} {x0},{y1}" '
                 f'fill="#fff" stroke="#000" stroke-width="1.5"/>')
        else:
            raise ValueError(k)
        return s + lines_at(cx, (y0 + y1) / 2, self.lines, self.fs, self.b)


def arrow(*pts, dash=None):
    return P(list(pts), 1.4, dash, end=True)


def yes_no(x, y, s, an='start'):
    return T(x, y, s, SM, an=an, it=True)


def conn(cx, cy, s):
    """Съединител (кръг с буква)."""
    return (f'<circle cx="{cx}" cy="{cy}" r="13" fill="#fff" stroke="#000" stroke-width="1.5"/>' +
            T(cx, cy + 4.5, s, 12.5, True))


def state(cx, cy, w, h, title, lines=(), fill='#fff'):
    check([title], w - 16, 13, True, where='state')
    check(lines, w - 16, 12, where='state')
    s = R(cx - w / 2, cy - h / 2, w, h, 1.7, fill, rx=14)
    n = len(lines)
    ty = cy - n * 8 + 4 if n else cy + 5
    s += T(cx, ty, title, 13, True)
    for i, t in enumerate(lines):
        s += T(cx, ty + 18 + i * 16, t, 12)
    return s


def label_lines(x, y, lines, fs=SM, an='start', lh=15, it=False):
    return ''.join(T(x, y + i * lh, t, fs, an=an, it=it) for i, t in enumerate(lines))


# ============================================================================
#  Фиг. 3.1 - структура на програмата и контексти на изпълнение
# ============================================================================
def item_row(x, y, w, name, desc, h=26):
    check([name], w * 0.6, 12, mono=True, where='ред')
    if tw(name, 12, mono=True) + tw(desc, 12) + 26 > w:
        _warn.append(f'ред: {name} + {desc}')
    return (R(x, y, w, h, 1.1, '#fff') + TM(x + 8, y + h / 2 + 4.3, name, 12) +
            T(x + w - 8, y + h / 2 + 4.3, desc, 12, an='end'))


def cb_item(x, y, w, name, desc, h=48):
    check([desc], w - 16, SM, where='обратна функция')
    return (R(x, y, w, h, 1.1, '#fff') + TM(x + 8, y + 19, name, 12.5, b=True) +
            T(x + 8, y + 38, desc, SM, an='start'))


def fig_3_1():
    H = 860
    s = ''
    # ---------------- главен възел
    s += R(10, 10, 760, 332, 1.2, '#fff', rx=8) + T(24, 34, 'ГЛАВЕН ВЪЗЕЛ – Master.ino', 14, True, 'start')
    s += R(24, 46, 358, 286, 1.6) + T(203, 66, 'Главен цикъл – loop()', 13, True)
    m_items = [('server.handleClient()', 'HTTP заявки'), ('ArduinoOTA.handle()', 'обновяване OTA'),
               ('MDNS.update()', 'достъп по име'), ('handleSerialCommands()', 'команди по UART'),
               ('processQueue()', 'опашка → ESP-NOW'), ('processPingTest()', 'тест PING'),
               ('printLogLine()', 'запис CSV'), ('micros()', 'време за цикъл, памет')]
    for i, (n, d) in enumerate(m_items):
        s += item_row(34, 78 + i * 30, 338, n, d)
    s += R(432, 46, 328, 140, 1.5, '#fff', '7 4') + T(596, 66, 'Обратни функции (контекст на SDK)', 12.5, True)
    check(['Обратни функции (контекст на SDK)'], 312, 12.5, True, where='заглавие')
    s += cb_item(444, 78, 304, 'OnDataRecv()', 'приема отчета: версия, номер, загуби, RTT')
    s += cb_item(444, 132, 304, 'OnDataSent()', 'потвърждение на MAC ниво и време до него')
    s += arrow((596, 186), (596, 206)) + T(604, 201, 'запис', SM, an='start', it=True)
    s += R(432, 206, 328, 126, 1.5, '#f4f4f4') + T(596, 226, 'Споделени данни (volatile)', 12.5, True)
    for i, t in enumerate(['lastT – последният приет отчет', 'currentRelayState[4] – релетата',
                           'waitingForDelivery, deliverySuccess', 'pingOutstanding, logPending']):
        s += T(444, 250 + i * 20, t, 12, an='start')
        check([t], 304, 12, where='споделени')
    s += arrow((432, 268), (382, 268)) + T(407, 260, 'четене', 11, it=True)

    # ---------------- подчинен възел
    y0 = 368
    s += R(10, y0, 760, 430, 1.2, '#fff', rx=8) + T(24, y0 + 24, 'ПОДЧИНЕН ВЪЗЕЛ – Slave.ino', 14, True, 'start')
    s += R(24, y0 + 36, 328, 140, 1.5, '#fff', '7 4') + T(188, y0 + 56, 'Обратни функции (контекст на SDK)', 12.5, True)
    s += cb_item(36, y0 + 68, 304, 'OnDataRecv()', 'проверява адрес, версия, дължина, номер')
    s += cb_item(36, y0 + 122, 304, 'OnDataSent()', 'брои потвърдените изпращания')
    s += arrow((188, y0 + 176), (188, y0 + 196)) + T(196, y0 + 191, 'запис', SM, an='start', it=True)
    s += R(24, y0 + 196, 328, 106, 1.5, '#f4f4f4') + T(188, y0 + 216, 'Споделени данни (volatile)', 12.5, True)
    for i, t in enumerate(['commandPending, pending… – команда', 'successPackets, failedPackets',
                           'btnEdges[2] – брой фронтове']):
        s += T(36, y0 + 240 + i * 20, t, 12, an='start')
        check([t], 304, 12, where='споделени')
    s += arrow((188, y0 + 322), (188, y0 + 302)) + T(196, y0 + 317, 'запис', SM, an='start', it=True)
    s += R(24, y0 + 322, 328, 94, 1.5, '#fff', '7 4') + T(188, y0 + 342, 'Прекъсвания (IRAM)', 12.5, True)
    s += cb_item(36, y0 + 354, 304, 'isrButtonToggle(), isrButtonOff()', 'броят фронтовете на бутоните')

    s += R(400, y0 + 36, 356, 256, 1.6) + T(578, y0 + 56, 'Главен цикъл – loop()', 13, True)
    s_items = [('handleSafetyCutoff()', 'защита при течност'), ('handleButtons()', 'бутони, филтър 60 ms'),
               ('handleEspNowIncoming()', 'приета команда'), ('handleSerialCommands()', 'команди по UART'),
               ('handleTelemetry()', 'DHT11, VCC, отчети'), ('printLinkWarnings()', 'съобщения'),
               ('micros()', 'време за цикъл, памет')]
    for i, (n, d) in enumerate(s_items):
        s += item_row(410, y0 + 68 + i * 30, 336, n, d)
    s += arrow((352, y0 + 249), (400, y0 + 249)) + T(376, y0 + 241, 'четене', 11, it=True)
    s += L(578, y0 + 292, 578, y0 + 318, 1.4, end=True, start=True)
    s += R(400, y0 + 318, 356, 98, 1.5, '#f4f4f4') + T(578, y0 + 338, 'Периферия', 12.5, True)
    for i, t in enumerate(['релета: D0, D1, D2, D8 · бутони: D5, D6', 'DHT11: D4 · HR202: D7 · напрежение: A0',
                           'UART: TX, RX · EEPROM: сектор от Flash']):
        s += T(578, y0 + 362 + i * 19, t, 12)
        check([t], 340, 12, where='периферия')

    # ---------------- ESP-NOW
    yc = 78 + 4 * 30 + 13                                   # processQueue()
    s += arrow((34, yc), (17, yc), (17, y0 + 92), (36, y0 + 92), dash='8 4')
    s += T(26, 358, 'ESP-NOW: команда, 20 B', 12, True, 'start')
    yt = y0 + 68 + 4 * 30 + 13                              # handleTelemetry()
    s += arrow((746, yt), (763, yt), (763, 102), (748, 102), dash='8 4')
    s += T(754, 358, 'ESP-NOW: отчет, 144 B', 12, True, 'end')
    s += caption(H, 'Фиг. 3.1. Структура на програмите и контексти на изпълнение')
    write('fig_3_1_struktura_programa', W, H, s)


# ============================================================================
#  Фиг. 3.2 - алгоритъм за обработка на командите по UART
# ============================================================================
def fig_3_2():
    s = ''
    cx, wm, wd = 390, 262, 280            # основна колона
    lx, wl = 118, 196                     # лява колона - действия
    rx_, wr = 655, 200                    # дясна колона - SET
    bus_l, bus_r, bus_e = 14, 770, 540    # шини към края

    b1 = B('term', cx, 16, 250, 34, ['handleSerialCommands()'])
    b2 = B('dec', cx, 70, wd, 64, ['Има приети', 'знаци?'])
    b3 = B('proc', cx, 154, wm, 46, ['Четене на реда до „\\n"', '(най-много 50 ms)'])
    b4 = B('dec', cx, 220, wd, 58, ['Празен ред?'])
    b5 = B('proc', cx, 298, wm, 46, ['Копие с главни букви (up);', 'ехо, паролите – скрити'])
    b6 = B('dec', cx, 364, wd, 62, ['Започва', 'със „SET "?'])
    b7 = B('dec', cx, 446, wd, 62, ['SAVE, PUSH', 'или FACTORY?'])
    b8 = B('dec', cx, 528, wd, 62, ['PING, LOG', 'или RESET STATS?'])
    b9 = B('dec', cx, 610, wd, 62, ['1…7, M или', 'R<n> ON/OFF?'])
    b10 = B('dec', cx, 692, wd, 62, ['Включване', 'при блокировка?'])
    b11 = B('proc', cx, 774, wm, 46, ['Изпълнение; командите за', 'релета – чрез опашката'])
    end = B('term', cx, 850, 250, 34, ['Край – връщане в loop()'])

    a1 = B('proc', lx, 453, wl, 48, ['Запис в EEPROM; CONFIG', 'в опашката; рестарт'])
    a2 = B('proc', lx, 535, wl, 48, ['Тест PING; запис CSV;', 'нулиране на броячите'])
    a3 = B('io', lx, 618, wl, 46, ['„Непозната команда"'])
    a4 = B('io', lx, 700, wl, 46, ['„Отказ – влага"'])

    s1 = B('proc', rx_, 371, wr, 48, ['Отделяне на ключа', 'и стойността'])
    s2 = B('dec', rx_, 439, wr, 60, ['Има', 'стойност?'])
    s3 = B('dec', rx_, 519, wr, 60, ['Познат', 'ключ?'])
    s4 = B('dec', rx_, 599, wr, 60, ['Допустима', 'стойност?'])
    s5 = B('proc', rx_, 679, wr, 48, ['Запис в cfg (RAM);', '„изпълнете SAVE"'])
    s6 = B('io', 664, 767, 192, 46, ['Съобщение за грешка'])

    for b in (b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, end, a1, a2, a3, a4, s1, s2, s3, s4, s5, s6):
        s += b.svg()
    # основна колона
    for u, v in ((b1, b2), (b2, b3), (b3, b4), (b4, b5), (b5, b6), (b6, b7), (b7, b8), (b8, b9), (b9, b10),
                 (b10, b11), (b11, end)):
        s += arrow(u.bot, v.top)
    for b, t in ((b2, 'да'), (b4, 'не'), (b6, 'не'), (b7, 'не'), (b8, 'не'), (b9, 'да'), (b10, 'не')):
        s += yes_no(cx + 6, b.y + b.h + 13, t)
    # изходи наляво
    for b, a, t in ((b7, a1, 'да'), (b8, a2, 'да'), (b10, a4, 'да')):
        s += arrow(b.lft, (a.cx + a.w / 2 - (7 if a.k == 'io' else 0), b.my)) + yes_no(b.lft[0] - 4, b.my - 6, t, 'end')
    s += arrow(b9.lft, (a3.cx + a3.w / 2 - 7, b9.my)) + yes_no(b9.lft[0] - 4, b9.my - 6, 'не', 'end')
    # лява шина към края
    s += arrow(b2.lft, (bus_l, b2.my), (bus_l, end.my), end.lft) + yes_no(b2.lft[0] - 4, b2.my - 6, 'не', 'end')
    s += P([b4.lft, (bus_l, b4.my)], 1.4) + dot(bus_l, b4.my, 2.8) + yes_no(b4.lft[0] - 4, b4.my - 6, 'да', 'end')
    for a in (a1, a2, a3, a4):
        xl = a.cx - a.w / 2 + (7 if a.k == 'io' else 0)
        s += P([(xl, a.my), (bus_l, a.my)], 1.4) + dot(bus_l, a.my, 2.8)
    # клон SET
    s += arrow(b6.rgt, s1.lft) + yes_no(b6.rgt[0] + 5, b6.my - 6, 'да')
    for u, v in ((s1, s2), (s2, s3), (s3, s4), (s4, s5)):
        s += arrow(u.bot, v.top)
    for b in (s2, s3, s4):
        s += yes_no(rx_ + 6, b.y + b.h + 13, 'да')
        s += P([b.rgt, (bus_r, b.my)], 1.4) + yes_no(b.rgt[0] + 3, b.my - 5, 'не')
        if b is not s2:
            s += dot(bus_r, b.my, 2.8)
    s += arrow((bus_r, s2.my), (bus_r, s6.my), (s6.cx + s6.w / 2 - 7, s6.my))
    # към края: успешен запис и съобщение за грешка
    ys = s5.bot[1] + 12
    s += P([s5.bot, (rx_, ys), (bus_e, ys), (bus_e, end.my)], 1.4)
    s += P([s6.bot, (s6.cx, 836), (bus_e, 836)], 1.4) + dot(bus_e, 836, 2.8)
    s += arrow((bus_e, end.my), end.rgt)
    H = 930
    s += caption(H, 'Фиг. 3.2. Алгоритъм за обработка на командите по UART')
    write('fig_3_2_algoritam_uart', W, H, s)


def TS(x, y, main, sub, fs=FS, an='middle', it=True, b=False):
    """Текст с долен индекс, например t_изп (подравняването се изчислява тук)."""
    st = 'italic' if it else 'normal'
    fsub = fs * 0.72
    wtot = tw(main, fs, b) + tw(sub, fsub, b)
    x0 = x - wtot / 2 if an == 'middle' else (x - wtot if an == 'end' else x)
    return (f'<text x="{x0:.1f}" y="{y}" font-family="DejaVu Sans, sans-serif" font-size="{fs}" '
            f'font-weight="{"bold" if b else "normal"}" text-anchor="start">'
            f'<tspan font-style="{st}">{main}</tspan><tspan dy="{fs * 0.3:.1f}" font-size="{fsub:.1f}">{sub}</tspan></text>')


def TR(x, y, s_, fs=SM, angle=-90, b=False):
    """Завъртян текст."""
    return (f'<text x="0" y="0" transform="translate({x},{y}) rotate({angle})" font-family="DejaVu Sans, sans-serif" '
            f'font-size="{fs}" font-weight="{"bold" if b else "normal"}" text-anchor="middle">{s_}</text>')


def curve(d, dash=None):
    ds = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<path d="{d}" fill="none" stroke="#000" stroke-width="1.4"{ds} marker-end="url(#a)"/>'


# ============================================================================
#  Фиг. 3.3 - краен автомат на опашката от команди
# ============================================================================
def fig_3_3():
    H = 520
    s = ''
    # опашка (хранилище на данни)
    s += R(170, 18, 440, 62, 1.5, '#f4f4f4') + T(390, 42, 'Кръгова опашка – 12 места', 13, True)
    s += T(390, 64, 'източници: уеб интерфейс, UART, PING, PUSH, RESET STATS', SM)
    check(['източници: уеб интерфейс, UART, PING, PUSH, RESET STATS'], 424, SM, where='опашка')
    # състояния
    s += state(150, 250, 200, 78, 'Q_IDLE', ['очакване', 'на команда'])
    s += state(630, 250, 220, 78, 'Q_WAIT_ACK', ['очакване на', 'потвърждение'])
    s += state(390, 430, 220, 78, 'Q_COOLDOWN', ['пауза между', 'командите'])
    # начално състояние
    s += dot(150, 140, 7) + arrow((150, 147), (150, 211))
    # опашка -> Q_IDLE
    s += curve('M 230 80 C 230 120, 205 150, 190 208', '6 4')
    s += label_lines(236, 116, ['първата', 'команда'], it=True)
    # Q_IDLE -> Q_WAIT_ACK
    s += arrow((250, 244), (520, 244))
    s += T(385, 234, 'опашката не е празна /', SM, it=True)
    s += label_lines(385, 262, ['sendControl(): seq + 1,', 'esp_now_send(), t₀ = millis()'], an='middle')
    # самопримки
    s += curve('M 95 211 C 95 170, 45 170, 55 211')
    s += T(70, 166, 'опашката е празна', SM, it=True)
    s += curve('M 600 211 C 600 172, 670 172, 665 211')
    s += T(635, 166, 'чакане', SM, it=True)
    # Q_WAIT_ACK -> Q_COOLDOWN (два прехода)
    s += arrow((590, 289), (590, 412), (500, 412))
    s += label_lines(582, 318, ['OnDataSent() /', 'премахване;', 'при неуспех –', 'грешка'], an='end', it=False)
    s += arrow((690, 289), (690, 452), (500, 452))
    s += label_lines(698, 318, ['> 500 ms /', 'премахване,', 'грешка'], an='start', it=False)
    # Q_COOLDOWN -> Q_IDLE
    s += arrow((280, 430), (150, 430), (150, 289))
    s += T(215, 422, '≥ 250 ms', SM, it=True)
    s += caption(H, 'Фиг. 3.3. Краен автомат на опашката от команди в главния възел')
    write('fig_3_3_opashka', W, H, s)


# ============================================================================
#  Фиг. 3.4 - последователност на обмена при управление от браузъра
# ============================================================================
def fig_3_4():
    H = 620
    s = ''
    xb, xm, xs, xr = 75, 292, 540, 712
    heads = ((xb, 116, 'Браузър'), (xm, 150, 'Главен възел'), (xs, 158, 'Подчинен възел'), (xr, 84, 'Реле 1'))
    for x, w, t in heads:
        s += R(x - w / 2, 16, w, 36, 1.5, '#f4f4f4', rx=4) + T(x, 39, t, 12.5, True)
        s += L(x, 52, x, 560, 1.1, '5 4')

    def msg(x1, x2, y, lines, dash=None, thin=False):
        r = P([(x1, y), (x2, y)], 1.0 if thin else 1.5, dash, end=True)
        xm_ = (x1 + x2) / 2
        for k, t in enumerate(reversed(lines)):
            r += T(xm_, y - 6 - k * 15, t, SM)
            check([t], abs(x2 - x1) - 6, SM, where='съобщение')
        return r

    def note(x, y, w, lines, h=None):
        h = h or 10 + 15 * len(lines)
        check(lines, w - 10, SM, where='бележка')
        r = R(x, y, w, h, 1.1, '#fff', rx=3)
        for k, t in enumerate(lines):
            r += T(x + 6, y + 15 + k * 15, t, SM, an='start')
        return r

    s += msg(xb, xm, 88, ['GET /toggle?id=0'])
    s += note(xb + 18, 100, xm - xb - 30, ['проверка: индекс,', 'блокировка; в опашката'])
    s += msg(xm, xb, 164, ['200 {"state": true}'], '6 3')
    s += msg(xm, xs, 200, ['ESP-NOW: CMD_RELAY, seq = n (20 B)'])
    s += msg(xs, xm, 226, ['ACK – MAC ниво'], '2 3', True)
    s += note(xb + 18, 240, xm - xb - 30, ['OnDataSent(): време до', 'потвърждението;', 'пауза 250 ms'])
    s += note(xs + 10, 214, 150, ['OnDataRecv():', 'проверки, в очакване'])
    s += note(xs + 10, 270, 150, ['loop(): applyRelay()'])
    s += msg(xs, xr, 322, ['D0 = 1'])
    s += note(xr - 104, 334, 96, ['задействане', '≤ 10 ms'])
    s += msg(xs, xm, 410, ['ESP-NOW: отчет „отговор на команда",', 'ackSeq = n (144 B)'], '6 3')
    s += note(xb + 18, 424, xm - xb - 30, ['OnDataRecv(): релетата,', 'броячи, закъснение'])
    s += msg(xb, xm, 500, ['GET /data (всяка секунда)'])
    s += msg(xm, xb, 536, ['JSON: "relays": [true, …]'], '6 3')
    # времена
    s += L(762, 200, 762, 366, 1.2) + L(756, 200, 768, 200, 1.2) + L(756, 366, 768, 366, 1.2)
    s += TR(752, 283, 'под 40 ms (оценка)', SM)
    s += T(390, 585, 'плътна линия – заявка или команда; прекъсната – отговор; точкова – потвърждение на MAC ниво', SM, it=True)
    s += caption(H, 'Фиг. 3.4. Последователност на обмена при включване на реле от браузъра')
    write('fig_3_4_posledovatelnost', W, H, s)


# ============================================================================
#  Фиг. 3.5 - времедиаграма на телеметрията и определения за времето за цикъл
# ============================================================================
def fig_3_5():
    H = 540
    s = ''
    s += T(20, 26, 'а) Цикъл на телеметрията в подчинения възел (не в мащаб)', 13, True, 'start')
    ya = 170
    s += L(26, ya, 762, ya, 1.5, end=True) + T(770, ya + 16, 't', 13, it=True)
    # итерации - къси черти
    xs_ = [x for x in range(34, 150, 9)] + [x for x in range(176, 266, 9)]
    samples = [382 + 18 * k for k in range(10)]
    xs_ += [x for x in range(376, 548, 9)] + [x for x in range(566, 752, 9)]
    for x in xs_:
        s += L(x, ya - 12, x, ya, 1.0)
    # прекъсване на оста
    s += L(154, ya + 8, 162, ya - 8, 1.3) + L(162, ya + 8, 170, ya - 8, 1.3)
    s += R(158, ya - 2, 8, 4, 0, '#fff').replace('stroke="#000"', 'stroke="#fff"')
    # DHT11
    s += R(272, ya - 30, 98, 30, 1.5, '#dcdcdc') + T(321, ya - 11, 'DHT11', 12, True)
    s += T(321, ya - 40, '≈ 25 ms', SM, it=True)
    s += arrow((272, 74), (272, 134)) + T(272, 66, 'изтича интервалът', SM, True)
    # 10 проби
    for x in samples:
        s += f'<polygon points="{x - 5},{ya - 16} {x + 5},{ya - 16} {x},{ya - 7}" fill="#000"/>'
    s += label_lines(462, ya + 24, ['10 проби на A0 през ≥ 2 ms (≈ 18 ms),', 'по една на итерация'], an='middle')
    # отчети
    s += arrow((556, 134), (556, 82)) + label_lines(556, 52, ['периодичен отчет;', 'затваряне на прозореца'], an='middle', fs=SM)
    s += arrow((40, 134), (40, 82)) + T(40, 72, 'предходен отчет', SM, an='start')
    # прозорец
    yb = 232
    s += L(40, yb, 556, yb, 1.2) + L(40, yb - 6, 40, yb + 6, 1.2) + L(556, yb - 6, 556, yb + 6, 1.2)
    s += T(298, yb + 18, 'прозорец на статистиката – интервалът на телеметрията (2 s)', SM)

    # ---- б) определения
    s += T(20, 288, 'б) Време за изпълнение и период на главния цикъл', 13, True, 'start')
    yy, hh = 350, 34
    s += R(80, yy, 220, hh, 1.5) + T(190, yy + 22, 'loop() – итерация i', 12)
    s += R(300, yy, 76, hh, 1.2, '#e6e6e6') + T(338, yy + 22, 'SDK', 12, True)
    s += R(376, yy, 220, hh, 1.5) + T(486, yy + 22, 'итерация i + 1', 12)
    s += R(596, yy, 76, hh, 1.2, '#e6e6e6') + T(634, yy + 22, 'SDK', 12, True)
    s += R(672, yy, 90, hh, 1.5) + T(717, yy + 22, '…', 12)
    s += L(80, yy - 14, 300, yy - 14, 1.2, end=True, start=True) + L(80, yy - 20, 80, yy - 4, 1.0) + L(300, yy - 20, 300, yy - 4, 1.0)
    s += TS(190, yy - 28, 't', 'изп', 13)
    s += L(80, yy + hh + 16, 376, yy + hh + 16, 1.2, end=True, start=True) + L(376, yy + hh + 4, 376, yy + hh + 22, 1.0) + L(80, yy + hh + 4, 80, yy + hh + 22, 1.0)
    s += T(228, yy + hh + 36, 'T', 13, it=True)
    yl = 452
    sp = tw(' ', 12)
    x1 = 20 + tw('t', 12) + tw('изп', 12 * 0.72) + sp
    s += TS(20, yl, 't', 'изп', 12, an='start') + T(x1, yl,
        '– време за изпълнение: от началото до края на loop() (работата на програмата);', 12, an='start')
    s += T(20, yl + 18, 'T', 12, an='start', it=True) + T(20 + tw('T', 12) + sp, yl + 18,
        '– период: между началата на две итерации; включва обслужването на радиостека', 12, an='start')
    s += T(20, yl + 36, 'и обратните функции от SDK между итерациите.', 12, an='start')
    s += caption(H, 'Фиг. 3.5. Времедиаграма на телеметрията и измерване на времето за цикъл')
    write('fig_3_5_telemetria_cikal', W, H, s)


# ============================================================================
#  Фиг. 3.6 - краен автомат на защитната блокировка
# ============================================================================
def fig_3_6():
    H = 500
    s = ''
    s += state(140, 200, 220, 86, 'НОРМАЛЕН РЕЖИМ', ['релетата се управляват', 'от всички източници'])
    s += state(605, 110, 290, 86, 'БЛОКИРОВКА – МОКРО', ['релетата са изключени,', 'включването е забранено'], '#f0f0f0')
    s += state(605, 330, 290, 86, 'БЛОКИРОВКА – ИЗЧАКВАНЕ', ['сухо по-малко от 2 s,', 'включването е забранено'], '#f0f0f0')
    s += dot(140, 118, 7) + arrow((140, 125), (140, 157))
    # нормален -> мокро
    s += arrow((250, 180), (300, 180), (300, 110), (460, 110))
    s += T(380, 100, 'DO = 0 (течност) /', SM, it=True)
    s += label_lines(380, 128, ['allRelaysOff(); брояч;', 'отчет „събитие"'], an='middle')
    # мокро <-> изчакване
    s += arrow((690, 153), (690, 287)) + label_lines(698, 214, ['DO = 1', '(сухо)'], an='start', it=True)
    s += arrow((520, 287), (520, 153)) + label_lines(512, 214, ['DO = 0 /', 'lastWetMs ← now'], an='end', it=True)
    # изчакване -> нормален
    s += arrow((460, 330), (140, 330), (140, 243))
    s += T(300, 322, 'сухо ≥ 2 s / отчет „събитие";', SM, it=True)
    s += T(300, 348, 'релетата остават изключени', SM, it=True)
    # самопримка на „мокро"
    s += curve('M 560 67 C 560 22, 650 22, 650 67')
    s += T(605, 20, 'DO = 0 / lastWetMs ← now', SM, it=True)
    # бележка
    s += R(24, 392, 732, 54, 1.1, '#fff', '5 3', 4)
    s += label_lines(36, 414, ['В състоянията на блокировка applyRelay(…, включено) се отказва и броячът на отказите',
                               'се увеличава; изключването е разрешено винаги. DO – изход на модула HR202 + LM393.'],
                     an='start', lh=18, fs=12)
    check(['В състоянията на блокировка applyRelay(…, включено) се отказва и броячът на отказите'], 716, 12, where='бележка')
    s += caption(H, 'Фиг. 3.6. Краен автомат на защитната блокировка при течност')
    write('fig_3_6_zashtita', W, H, s)


# ============================================================================
#  Фиг. 3.7 и 3.8 - алгоритъм на управляващата програма
# ============================================================================
def algorithm(name, cap, setup, side, fail, loop_items, async_lines):
    """setup - [(вид, редове)]; side - (индекс на решението, 'да'/'не' встрани);
    fail - блоковете в края на страничния клон; loop_items - [(вид, редове)]."""
    s = ''
    cx1, cx2, w = 195, 588, 320
    bus = 392
    y = 16
    blocks = []
    for kind, lines in setup:
        h = 34 if kind == 'term' else (72 if kind == 'dec' else 44)
        ww = 200 if kind == 'term' else (300 if kind == 'dec' else w)
        blocks.append(B(kind, cx1, y, ww, h, lines))
        y += h + 16
    for b in blocks:
        s += b.svg()
    for u, v in zip(blocks, blocks[1:]):
        s += arrow(u.bot, v.top)
    di, side_lbl, down_lbl = side
    d = blocks[di]
    s += yes_no(cx1 + 6, d.y + d.h + 13, down_lbl)
    # съединител към цикъла
    yA = y + 13
    s += arrow(blocks[-1].bot, (cx1, yA - 13)) + conn(cx1, yA, 'A')
    # страничен клон
    y = yA + 30
    fb = []
    for kind, lines in fail:
        h = 34 if kind == 'term' else 44
        fb.append(B(kind, cx1, y, 200 if kind == 'term' else w, h, lines))
        y += h + 16
    for b in fb:
        s += b.svg()
    for u, v in zip(fb, fb[1:]):
        s += arrow(u.bot, v.top)
    s += arrow(d.rgt, (bus, d.my), (bus, fb[0].my), fb[0].rgt) + yes_no(d.rgt[0] + 5, d.my - 6, side_lbl)
    y_end = y
    # цикъл
    s += conn(cx2, 25, 'A')
    y = 78
    lb = []
    for kind, lines in loop_items:
        lb.append(B(kind, cx2, y, w, 44, lines))
        y += 60
    for b in lb:
        s += b.svg()
    s += L(cx2, 38, cx2, 58, 1.4) + arrow((cx2, 58), lb[0].top)
    for u, v in zip(lb, lb[1:]):
        s += arrow(u.bot, v.top)
    yl = lb[-1].bot[1] + 14
    s += P([lb[-1].bot, (cx2, yl), (766, yl), (766, 58)], 1.4) + arrow((766, 58), (cx2 + 1, 58)) + dot(cx2, 58, 3)
    # асинхронни обратни функции
    ya = yl + 30
    ha = 32 + 17 * len(async_lines)
    s += R(cx2 - w / 2, ya, w, ha, 1.4, '#fff', '7 4', 4)
    s += T(cx2, ya + 19, 'Асинхронно – контекст на SDK', 12.5, True)
    for k, t in enumerate(async_lines):
        s += T(cx2 - w / 2 + 10, ya + 38 + k * 17, t, SM, an='start')
        check([t], w - 20, SM, where='асинхронно')
    H = max(y_end, ya + ha) + 44
    s += caption(H, cap)
    write(name, W, H, s)


def fig_3_7():
    setup = [('term', ['Начало']),
             ('proc', ['Serial: 115 200 bit/s;', 'изчакване на ред ≤ 50 ms']),
             ('proc', ['EEPROM.begin(512);', 'зареждане на конфигурацията']),
             ('proc', ['Wi-Fi: станция, без пестене', 'на енергия; свързване към SSID']),
             ('dec', ['Свързан до 20 s?', '(междувременно – UART)']),
             ('proc', ['Случаен начален номер', 'на командите (ESP.random())']),
             ('proc', ['Проверка на канала; при', 'разлика – предупреждение']),
             ('proc', ['mDNS и LLMNR', '(http://smarthome.local)']),
             ('proc', ['ESP-NOW: роля COMBO, обратни', 'функции; peer – подчиненият']),
             ('proc', ['Уеб сървър: /, /data, /config,', '/toggle, /onAll, /offAll, …']),
             ('proc', ['OTA с парола;', 'меню по UART'])]
    fail = [('proc', ['Само UART', 'в продължение на 120 s']), ('term', ['ESP.restart()'])]
    loop = [('proc', ['t₀ = micros(); период спрямо', 'предходната итерация']),
            ('sub', ['server.handleClient()', '– HTTP заявки']),
            ('sub', ['ArduinoOTA.handle();', 'MDNS.update()']),
            ('sub', ['handleSerialCommands()', '– UART (Фиг. 3.2)']),
            ('sub', ['processQueue()', '– опашка (Фиг. 3.3)']),
            ('sub', ['processPingTest()', '– тест PING']),
            ('sub', ['printLogLine()', '– ред CSV при нов отчет']),
            ('proc', ['време за изпълнение: micros() − t₀;', 'памет; прозорец 2 s'])]
    asy = ['OnDataRecv(): отчет → lastT, релета,', 'броячи, закъснение PING',
           'OnDataSent(): потвърждение → опашката']
    algorithm('fig_3_7_algoritam_master', 'Фиг. 3.7. Алгоритъм на управляващата програма на главния възел',
              setup, (4, 'не', 'да'), fail, loop, asy)


def fig_3_8():
    setup = [('term', ['Начало']),
             ('proc', ['Serial: 115 200 bit/s']),
             ('proc', ['Изходи на релетата: LOW,', 'след това OUTPUT']),
             ('proc', ['Бутони и HR202 – вход', 'с подтеглящ резистор']),
             ('proc', ['EEPROM.begin(256);', 'зареждане на конфигурацията']),
             ('dec', ['SB1 натиснат', '3 s при стартиране?']),
             ('proc', ['DHT11; причина за рестарт;', 'прекъсвания на бутоните']),
             ('proc', ['Първо измерване на VCC', '(10 проби, ≈ 20 ms)']),
             ('proc', ['Wi-Fi AP + STA; скрита точка', 'за достъп на работния канал']),
             ('proc', ['ESP-NOW: роля COMBO, обратни', 'функции; peer – главният']),
             ('proc', ['Меню по UART'])]
    fail = [('proc', ['Фабрични настройки;', 'запис в EEPROM']), ('term', ['ESP.restart()'])]
    loop = [('proc', ['t₀ = micros(); период спрямо', 'предходната итерация']),
            ('sub', ['handleSafetyCutoff()', '– защита (Фиг. 3.6)']),
            ('sub', ['handleButtons()', '– бутони, филтър 60 ms']),
            ('sub', ['handleEspNowIncoming()', '– изпълнение на командата']),
            ('sub', ['handleSerialCommands()', '– UART']),
            ('sub', ['handleTelemetry() – DHT11,', 'напрежение, отчети (Фиг. 3.5)']),
            ('sub', ['printLinkWarnings()', '– отхвърлени пакети']),
            ('proc', ['време за изпълнение: micros() − t₀;', 'най-малка свободна памет'])]
    asy = ['OnDataRecv(): проверки на адреса,', 'версията и номера → команда в очакване',
           'OnDataSent(): броячи на изпращанията', 'Прекъсвания SB1, SB2: брой фронтове']
    algorithm('fig_3_8_algoritam_slave', 'Фиг. 3.8. Алгоритъм на управляващата програма на подчинения възел',
              setup, (5, 'да', 'не'), fail, loop, asy)


if __name__ == '__main__':
    os.makedirs('figuri/png', exist_ok=True)
    for f in (fig_3_1, fig_3_2, fig_3_3, fig_3_4, fig_3_5, fig_3_6, fig_3_7, fig_3_8):
        f()
    for w in _warn:
        print('  ! не се побира:', w)

#!/usr/bin/env python3
"""Генерира листовете на графичната част (A1, основен надпис по ЕСКД).
Употреба:  python3 grafichna_chast/generirane_listove.py   (от папката diplomna_rabota)
Изисква:   cairosvg
"""
import re, zlib, xml.etree.ElementTree as ET
import cairosvg

F = 'DejaVu Sans, Liberation Sans, Arial, sans-serif'
W, H = 841, 594                     # A1 хоризонтално, 1 единица = 1 mm
FL, FR, FT, FB = 20, 10, 10, 10     # поле 20 mm за подшиване
CX, CY, CW = 26, 16, 785            # работна зона
OUT = 'grafichna_chast/'

def T(x, y, t, fs=3.2, b=False, an='start', fill='#000'):
    return (f'<text x="{x}" y="{y}" font-family="{F}" font-size="{fs}" '
            f'font-weight="{"bold" if b else "normal"}" text-anchor="{an}" fill="{fill}">{t}</text>')
def R(x, y, w, h, sw=0.35):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="none" stroke="#000" stroke-width="{sw}"/>'
def L(x1, y1, x2, y2, sw=0.35):
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#000" stroke-width="{sw}"/>'

def place(path, x, y, maxw, maxh, subs=()):
    """Влага фигура като вложен <svg>; връща (svg, x, y, w, h) на реалното място.
    subs - замени в надписите (препратките към фигури от записката)."""
    s = open(path, encoding='utf-8').read()
    for a, b in subs:
        s = s.replace(a, b)
    vw, vh = map(float, re.search(r'viewBox="([\d. ]+)"', s).group(1).split()[2:])
    inner = re.sub(r'^.*?<svg[^>]*>', '', s, flags=re.S)
    inner = re.sub(r'</svg>\s*$', '', inner, flags=re.S)
    inner = re.sub(r'<text[^>]*>Фиг\.[^<]*</text>', '', inner)            # надписът е в основния надпис
    inner = re.sub(r'<rect width="\d+" height="\d+" fill="#fff"/>', '', inner, count=1)
    tag = "s" + str(zlib.crc32(path.encode()) % 9973)          # стабилно между стартиранията
    for i in set(re.findall(r'id="([^"]+)"', inner)):                    # уникални id на маркерите
        inner = inner.replace(f'id="{i}"', f'id="{i}{tag}"').replace(f'url(#{i})', f'url(#{i}{tag})')
    k = min(maxw / vw, maxh / vh); w, h = vw * k, vh * k
    ox, oy = x + (maxw - w) / 2, y + (maxh - h) / 2
    return (f'<svg x="{ox:.2f}" y="{oy:.2f}" width="{w:.2f}" height="{h:.2f}" '
            f'viewBox="0 0 {vw} {vh}">{inner}</svg>', ox, oy, w, h)

def title_block(naim, podraz, shifr, n, total):
    """Основен надпис 185 x 55 mm, форма 1. naim може да е низ или списък от редове."""
    lines = [naim] if isinstance(naim, str) else naim
    x0, y0 = W - FR - 185, H - FB - 55
    b = R(x0, y0, 185, 55, 0.7) + L(x0 + 65, y0, x0 + 65, y0 + 55, 0.7)
    for i in range(1, 11): b += L(x0, y0 + i * 5, x0 + 65, y0 + i * 5)
    c1 = [7, 10, 23, 15, 10]; a = 0
    for c in c1[:-1]: a += c; b += L(x0 + a, y0, x0 + a, y0 + 25)
    a = 0
    for lbl, c in zip(('Изм.', 'Лист', '№ докум.', 'Подп.', 'Дата'), c1):
        b += T(x0 + a + c / 2, y0 + 23.4, lbl, 2.4, an='middle'); a += c
    c2 = [17, 26, 12, 10]; a = 0
    for c in c2[:-1]: a += c; b += L(x0 + a, y0 + 25, x0 + a, y0 + 55)
    for i, (r, who) in enumerate([('Разраб.', 'Праматаров К. Т.'), ('Пров.', 'Богданов Л.'),
                                  ('Консулт.', ''), ('Т. контр.', ''), ('Н. контр.', ''),
                                  ('Утв.', 'Якимов П.')]):
        yy = y0 + 25 + i * 5; b += T(x0 + 1.5, yy + 3.6, r, 2.4)
        if who: b += T(x0 + 18, yy + 3.6, who, 2.5)
    b += T(x0 + 67, y0 + 4.5, 'Технически университет – София', 2.6)
    b += T(x0 + 67, y0 + 8.5, 'ФЕТТ, катедра „Електронна техника"', 2.6) + L(x0 + 65, y0 + 10, W - FR, y0 + 10)
    b += T(x0 + 67, y0 + 14.6, 'Система за управление на дома', 2.5, fill='#333')
    b += T(x0 + 67, y0 + 18.2, 'чрез IEEE 802.11 (Wi-Fi) интерфейс', 2.5, fill='#333') + L(x0 + 65, y0 + 20, W - FR, y0 + 20)
    # кирилските главни получер са ~0,81 от кегела -> 140/дължина държи реда в 112 mm
    fs = min(5.2 if len(lines) == 1 else 4.8, min(140 / max(len(l), 1) for l in lines))
    if len(lines) == 1:
        b += T(x0 + 125, y0 + 29, lines[0], fs, True, 'middle')
        if podraz: b += T(x0 + 125, y0 + 34.5, podraz, 2.8, an='middle')
    else:
        for i, l in enumerate(lines): b += T(x0 + 125, y0 + 25.5 + i * 6, l, fs, True, 'middle')
        if podraz: b += T(x0 + 125, y0 + 35.8, podraz, 2.8, an='middle')
    b += L(x0 + 65, y0 + 37, W - FR, y0 + 37) + L(x0 + 105, y0 + 37, x0 + 105, y0 + 47) + L(x0 + 145, y0 + 37, x0 + 145, y0 + 47)
    b += L(x0 + 65, y0 + 47, W - FR, y0 + 47)
    for lbl, val, dx in (('Литера', 'Д', 20), ('Маса', '—', 60), ('Мащаб', '—', 100)):
        b += T(x0 + 65 + dx, y0 + 40.6, lbl, 2.4, an='middle', fill='#555') + T(x0 + 65 + dx, y0 + 45.4, val, 3.0, True, 'middle')
    b += L(x0 + 105, y0 + 47, x0 + 105, y0 + 55) + L(x0 + 145, y0 + 47, x0 + 145, y0 + 55)
    b += T(x0 + 85, y0 + 51.6, f'Лист {n}', 3.0, True, 'middle') + T(x0 + 125, y0 + 51.6, f'Листа {total}', 3.0, True, 'middle')
    b += T(x0 + 165, y0 + 51.6, 'Спец.: ЕСХЕ', 2.6, an='middle')
    b += R(x0, y0 - 8, 185, 8, 0.5) + T(x0 + 92.5, y0 - 2.4, shifr, 3.4, True, 'middle')
    return b

def write_sheet(name, naim, podraz, shifr, n, content):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">'
           f'<rect width="{W}" height="{H}" fill="#fff"/>{R(FL, FT, W - FL - FR, H - FT - FB, 0.7)}'
           f'{content}{title_block(naim, podraz, shifr, n, 3)}</svg>')
    p = OUT + name + '.svg'
    open(p, 'w', encoding='utf-8').write(svg); ET.parse(p)
    cairosvg.svg2pdf(url=p, write_to=OUT + name + '.pdf')
    cairosvg.svg2png(url=p, write_to=OUT + name + '.png', output_width=3300)
    print('  ✓', name)

def spec_table():
    elems = [('DD1', 'Развойна платка', 'NodeMCU v3 (ESP8266EX, CH340)', '1'),
             ('A1…A4', 'Релеен модул', '1 канал, 5 V, оптрон, избор H/L', '4'),
             ('A5', 'Датчик за влага (модул)', 'HR202 + LM393, изход DO', '1'),
             ('BK1', 'Датчик за темп. и влажност', 'DHT11', '1'),
             ('SB1, SB2', 'Бутон', 'тактов 6 × 6 mm, н.о.', '2'),
             ('R1', 'Резистор', '47 kΩ, 0,25 W', '1'), ('R2', 'Резистор', '20 kΩ, 0,25 W', '1'),
             ('C1', 'Кондензатор електролитен', '1000 µF, 16 V', '1')]
    wc = [26, 58, 72, 12]; rh = 5.6
    x0 = W - FR - sum(wc); y0 = H - FB - 63 - rh * (len(elems) + 1) - 3
    b = R(x0, y0, sum(wc), rh * (len(elems) + 1), 0.6); a = 0
    for c in wc[:-1]: a += c; b += L(x0 + a, y0, x0 + a, y0 + rh * (len(elems) + 1), 0.4)
    for i in range(1, len(elems) + 1): b += L(x0, y0 + i * rh, x0 + sum(wc), y0 + i * rh, 0.3)
    b += f'<rect x="{x0}" y="{y0}" width="{sum(wc)}" height="{rh}" fill="#efefef" stroke="#000" stroke-width="0.6"/>'
    a = 0
    for lbl, c in zip(('Поз. обозн.', 'Наименование', 'Тип / параметри', 'Кол.'), wc):
        b += T(x0 + a + c / 2, y0 + 3.9, lbl, 2.7, True, 'middle'); a += c
    for i, (p, nm, t, q) in enumerate(elems):
        yy = y0 + (i + 1) * rh + 3.9
        b += T(x0 + 13, yy, p, 2.6, an='middle') + T(x0 + 28, yy, nm, 2.6) + T(x0 + 86, yy, t, 2.6) + T(x0 + 162, yy, q, 2.6, an='middle')
    b += T(x0, y0 - 2.5, 'Спецификация на елементите', 3.2, True)
    return b, y0

def main():
    # Лист 1 – т. 5.1
    write_sheet('List_1_Blokova_shema', 'БЛОКОВА СХЕМА', 'Хардуер на системата', 'ДР.901322003.01.С1', 1,
                place('figuri/fig_2_1_blokova_shema.svg', CX, CY, CW, H - FT - FB - 12 - 70)[0])
    # Лист 2 – т. 5.2
    sp, ty = spec_table()
    # вътрешна схема на модула A5 - вторият датчик (HR202) в свободното поле долу вляво
    det, dx, dy, dw, dh = place('figuri/fig_2_5_hr202_modul.svg', CX + 30, 454, 240, 126)
    tx = dx + dw + 8
    det += T(tx, dy + 22, 'A5 – датчик за влага HR202', 3.8, True)
    det += T(tx, dy + 29, 'вътрешна схема на модула: B1 – влагочувствителен', 3.2)
    det += T(tx, dy + 34.5, 'резистор HR202, DA1 – компаратор LM393', 3.2)
    det += T(tx, dy + 42, 'Позиционните означения в тази схема се отнасят', 3.0, fill='#333')
    det += T(tx, dy + 47, 'само за модула A5.', 3.0, fill='#333')
    write_sheet('List_2_Principna_shema', 'ПРИНЦИПНА ЕЛЕКТРИЧЕСКА СХЕМА', 'Подчинен възел', 'ДР.901322003.02.С2', 2,
                place('figuri/fig_2_7_principna_shema.svg', CX, CY, CW, ty - CY - 8)[0] + det + sp)
    # Лист 3 – т. 5.3
    cells = [('figuri/fig_3_7_algoritam_master.svg', 'а) Алгоритъм на главния възел', CX, CY, 250, 290),
             ('figuri/fig_3_8_algoritam_slave.svg', 'б) Алгоритъм на подчинения възел', CX + 260, CY, 250, 290),
             ('figuri/fig_3_2_algoritam_uart.svg', 'в) Обработка на командите по UART', CX + 520, CY, 250, 290),
             ('figuri/fig_3_3_opashka.svg', 'г) Краен автомат на опашката от команди', CX, CY + 308, 380, 186),
             ('figuri/fig_3_6_zashtita.svg', 'д) Краен автомат на защитната блокировка', CX + 395, CY + 308, 380, 186)]
    subs = ((' (Фиг. 3.2)', ' (в)'), (' (Фиг. 3.3)', ' (г)'), (' (Фиг. 3.6)', ' (д)'),
            (' (Фиг. 3.5)', ''), (' (Листинг 3.3)', ''))
    alg = ''
    for path, lbl, x, y, mw, mh in cells:
        svg, ox, oy, w, h = place(path, x, y, mw, mh, subs)
        alg += svg + T(ox + w / 2, oy + h + 6, lbl, 3.6, True, 'middle')
    write_sheet('List_3_Algoritam', ['АЛГОРИТЪМ НА', 'УПРАВЛЯВАЩАТА ПРОГРАМА'], 'Главен и подчинен възел',
                'ДР.901322003.03.С3', 3, alg)

if __name__ == '__main__':
    main()

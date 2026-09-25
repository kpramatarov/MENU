# -*- coding: utf-8 -*-
"""Създава работната книга за попълване на измерванията (Глава 4)."""
import sys
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.comments import Comment

OUT = sys.argv[1] if len(sys.argv) > 1 else "firmware/Izmervania_v3.xlsx"

FONT = "Arial"
F_TITLE = Font(name=FONT, size=14, bold=True)
F_H2 = Font(name=FONT, size=11, bold=True)
F_TXT = Font(name=FONT, size=10)
F_BOLD = Font(name=FONT, size=10, bold=True)
F_EX = Font(name=FONT, size=10, italic=True, color="808080")
F_NOTE = Font(name=FONT, size=9, italic=True, color="595959")
FILL_IN = PatternFill("solid", fgColor="FFF2CC")     # попълва се
FILL_CALC = PatternFill("solid", fgColor="EDEDED")   # изчислява се
FILL_HEAD = PatternFill("solid", fgColor="D9E1F2")
FILL_LABEL = PatternFill("solid", fgColor="F7F7F7")
THIN = Side(style="thin", color="A6A6A6")
BORDER = Border(left=THIN, right=THIN, top=THIN, bottom=THIN)
WRAP = Alignment(wrap_text=True, vertical="top")
CENTER = Alignment(horizontal="center", vertical="center", wrap_text=True)

wb = Workbook()
wb.remove(wb.active)
yes_no_ranges = []   # (лист, диапазон) за падащ списък Да/Не


def sheet(name, title, widths):
    ws = wb.create_sheet(name)
    ws.sheet_view.showGridLines = False
    for i, w in enumerate(widths):
        ws.column_dimensions[chr(ord("A") + i)].width = w
    ws["A1"] = title
    ws["A1"].font = F_TITLE
    ws.page_setup.orientation = "landscape"
    ws.page_setup.fitToWidth = 1
    ws.page_setup.fitToHeight = 0
    ws.sheet_properties.pageSetUpPr.fitToPage = True
    return ws


def text(ws, row, lines, font=F_TXT, col=1, span=None):
    """Пише редове текст; при span обединява клетките по ширина."""
    for i, line in enumerate(lines):
        c = ws.cell(row=row + i, column=col, value=line)
        c.font = font
        c.alignment = Alignment(wrap_text=False, vertical="top")
    return row + len(lines)


def header(ws, row, titles, col=1):
    for j, t in enumerate(titles):
        c = ws.cell(row=row, column=col + j, value=t)
        c.font = F_BOLD
        c.fill = FILL_HEAD
        c.border = BORDER
        c.alignment = CENTER
    ws.row_dimensions[row].height = 45


def example(ws, row, values, col=1, fmts=None):
    """Ред с пример за формата - сив курсив, над заглавието на таблицата."""
    for j, v in enumerate(values):
        c = ws.cell(row=row, column=col + j, value=v)
        c.font = F_EX
        c.alignment = Alignment(horizontal="center" if j else "left")
        if fmts and j < len(fmts) and fmts[j]:
            c.number_format = fmts[j]


def cell(ws, ref, value=None, kind="in", fmt=None, bold=False):
    c = ws[ref]
    if value is not None:
        c.value = value
    c.border = BORDER
    if kind == "in":
        c.fill = FILL_IN
        c.font = F_TXT
    elif kind == "calc":
        c.fill = FILL_CALC
        c.font = F_BOLD if bold else F_TXT
    else:                                   # етикет
        c.fill = FILL_LABEL
        c.font = F_BOLD if bold else F_TXT
    c.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
    if fmt:
        c.number_format = fmt
    return c


def note(ws, ref, txt):
    ws[ref].comment = Comment(txt, "Шаблон")


# ============================================================================
#  НАЧАЛО
# ============================================================================
ws = sheet("Начало", "Измервания за Глава 4 — фърмуер v3", [5, 30, 16, 12, 34, 12])
r = text(ws, 3, [
    "Попълвайте само ЖЪЛТИТЕ клетки. СИВИТЕ се изчисляват сами. Сивият курсивен ред над всяка таблица е пример за формата — не е измерване.",
    "Празна клетка е по-добре от предположение: в дипломната работа влизат само измерени стойности. Пишете числата с единиците от заглавието на колоната.",
    "Изпратете ми: този файл, попълнен + файловете със записа от серийния порт + снимките.",
])
ws["A7"] = "Легенда:"; ws["A7"].font = F_BOLD
cell(ws, "B7", "попълва се", "in")
cell(ws, "C7", "изчислява се", "calc")
ex = ws["E7"]; ex.value = "пример (не е измерване)"; ex.font = F_EX

ws["A9"] = "Общи данни"; ws["A9"].font = F_H2
gen = [("Мултиметър — производител и модел", None, None),
       ("Обхват за постоянно напрежение (напр. 20 V)", None, None),
       ("Обхват за ток (напр. 200 mA / 10 A)", None, None),
       ("Дата на измерванията", None, "dd.mm.yyyy"),
       ("Фърмуер v3 и на двата модула? (Да/Не)", "Да", None),
       ("Разстояние между модулите при Е1–Е9, m", None, "0.0")]
for i, (lbl, val, fmt) in enumerate(gen):
    rr = 10 + i
    cell(ws, f"B{rr}", lbl, "label")
    ws.merge_cells(f"B{rr}:D{rr}")
    cell(ws, f"E{rr}", val, "in", fmt)
yes_no_ranges.append((ws, "E14"))

ws["A17"] = "Къде се виждат резултатите"; ws["A17"].font = F_H2
text(ws, 18, [
    "• LOG ON, PING и 7 се изпълняват и показват по UART на ГЛАВНИЯ модул — Serial Monitor (115200) или PuTTY.",
    "• LOG ON: на всеки 2 s ред „LOG,…“ (първият ред е заглавието на колоните). Това са данни за графиките — не се преписват, а се изпраща файлът.",
    "• PING 100: ~30 s, по един ред „PING,№,RTT в µs“ (или LOST), накрая „📶 Край на теста…“ и „RTT мин / ср / макс: … µs“.",
    "• Същите резултати от PING се виждат и в уеб страницата — картата „⏱️ Измервания“; тя показва текущите стойности, но не пази история.",
    "• Преди всеки опит: RESET STATS (или бутонът „НУЛИРАЙ СТАТИСТИКАТА“).",
])
ws["A24"] = "Запис във файл с PuTTY (задължително за опити над ~10 min)"; ws["A24"].font = F_H2
text(ws, 25, [
    "1. Затворете Serial Monitor на Arduino IDE — портът се ползва само от една програма.",
    "2. Session: Connection type = Serial; Serial line = COMx (Arduino IDE → Tools → Port); Speed = 115200.",
    "3. Connection → Serial: Flow control = None.",
    "4. Terminal: Local echo = Force on; Local line editing = Force on  ← без това командите не се разпознават.",
    "5. Window → Translation: Remote character set = UTF-8 (за кирилицата).",
    "6. Session → Logging: All session output; Log file name, напр. C:\\izmervania\\master_&Y&M&D_&T.log",
    "7. Session → Saved Sessions: ESP Master → Save → Open. Ако главният модул се рестартира при отварянето — изчакайте ~10 s.",
    "8. За дългите опити изключете заспиването на компютъра (Settings → Power → Sleep: Never).",
    "За кратки опити (PING, до ~10 min LOG) е достатъчен и Serial Monitor: маркирайте текста, Ctrl+C, поставете в Notepad и запишете.",
])

ws["A35"] = "Ред на опитите"; ws["A35"].font = F_H2
header(ws, 36, ["№", "Опит", "Лист", "Време", "Нужно", "Готово?"])
plan = [
    ("Е3", "Точност на VCC (A0)", "Е3 VCC", "20 min", "мултиметър"),
    ("Е6", "Бутони и филтър", "Е6 Бутони", "10 min", "—"),
    ("Е7", "Защита от влага", "Е7 Влага", "15 min", "мултиметър, вода"),
    ("Е8", "Закъснение (PING)", "Е8 PING", "10 min", "компютър (UART на главния)"),
    ("Е1", "Време за цикъл", "Е1 Цикъл", "30 min", "компютър + PuTTY, 3 клиента"),
    ("Е12", "Поведение при включване", "Е12 Включване", "5 min", "—"),
    ("Е9", "Надеждност на връзката", "Е9 Надеждност", "≥ 1 h (без намеса)", "компютър + PuTTY"),
    ("Е4", "Нива на изводите (по желание)", "Е4 Изводи", "10 min", "мултиметър"),
    ("Е5", "Ток на релетата (по желание)", "Е5 Ток", "10 min", "мултиметър (mA)"),
    ("Е2", "DHT11 и максимумът (по желание)", "Е2 DHT11", "15 min", "компютър + PuTTY"),
    ("Е11", "Дълготраен тест (по желание)", "Е11 24 часа", "24 h", "компютър + PuTTY"),
    ("Е10", "Обхват (по желание)", "Е10 Обхват", "20 min", "power bank"),
    ("Е13", "Отговор на уеб сървъра (по желание)", "Е13 Уеб", "10 min", "Chrome (F12)"),
    ("—", "Снимки", "Снимки", "15 min", "телефон"),
]
for i, row in enumerate(plan):
    rr = 37 + i
    for j, v in enumerate(row):
        cell(ws, f"{chr(65 + j)}{rr}", v, "label", bold=(j == 0))
    cell(ws, f"F{rr}", None, "in")
yes_no_ranges.append((ws, f"F37:F{36 + len(plan)}"))

# ============================================================================
#  Е1 ВРЕМЕ ЗА ЦИКЪЛ
# ============================================================================
ws = sheet("Е1 Цикъл", "Е1. Време за цикъл на двата модула", [22, 12, 13, 12, 12, 34, 30])
text(ws, 3, [
    "Числата се вземат от записа (LOG ON) — тук записвате само КОГА какъв режим е бил. RESET STATS в началото.",
    "Главният модул е свързан към компютъра, PuTTY записва във файл → LOG ON → по 10 min във всеки режим; отбелязвайте смяната на режима с Enter в PuTTY.",
    "Не пишете в UART на подчинения модул по време на опита. Накрая: LOG OFF.",
])
example(ws, 7, ["Пример:", 1, "25.09.2026", "14:05", "14:15", "master_20260925_140312.log", "телефон, Wi-Fi"])
header(ws, 8, ["Режим", "Брой уеб клиенти", "Дата", "Начален час", "Краен час", "Файл със записа", "Бележка"])
rows = [("А — без браузър", 0), ("Б — 1 клиент", 1), ("В — 3 клиента", 3),
        ("v2 за сравнение (по желание)", None)]
for i, (lbl, n) in enumerate(rows):
    rr = 9 + i
    cell(ws, f"A{rr}", lbl, "label", bold=True)
    cell(ws, f"B{rr}", n, "in" if n is None else "label")
    cell(ws, f"C{rr}", None, "in", "dd.mm.yyyy")
    cell(ws, f"D{rr}", None, "in", "hh:mm")
    cell(ws, f"E{rr}", None, "in", "hh:mm")
    cell(ws, f"F{rr}", None, "in")
    cell(ws, f"G{rr}", None, "in")
text(ws, 14, ["v2 за сравнение: v2_original/Master + v2_izmervane/Slave, запис от UART на ПОДЧИНЕНИЯ модул (редове V2LOOP), после отново v3."],
     font=F_NOTE)

# ============================================================================
#  Е2 DHT11
# ============================================================================
ws = sheet("Е2 DHT11", "Е2. Причина за максималното време — четенето на DHT11", [18, 13, 12, 12, 34, 30])
text(ws, 3, [
    "С LOG ON на главния: SET INTERVAL 500, SAVE, PUSH → 5 min; SET INTERVAL 5000, SAVE, PUSH → 5 min; накрая SET INTERVAL 2000, SAVE, PUSH.",
    "Резултатът е във файла със записа — тук се записват само часовете.",
])
example(ws, 6, ["Пример:", "25.09.2026", "15:00", "15:05", "master_20260925_145800.log", ""])
header(ws, 7, ["Интервал, ms", "Дата", "Начален час", "Краен час", "Файл със записа", "Бележка"])
for i, v in enumerate([500, 5000]):
    rr = 8 + i
    cell(ws, f"A{rr}", v, "label", bold=True)
    cell(ws, f"B{rr}", None, "in", "dd.mm.yyyy")
    cell(ws, f"C{rr}", None, "in", "hh:mm")
    cell(ws, f"D{rr}", None, "in", "hh:mm")
    cell(ws, f"E{rr}", None, "in")
    cell(ws, f"F{rr}", None, "in")

# ============================================================================
#  Е3 VCC
# ============================================================================
ws = sheet("Е3 VCC", "Е3. Точност на измерването на напрежението (A0)",
           [9, 10, 12, 12, 12, 13, 12, 14, 15, 15, 14, 12])
text(ws, 3, [
    "Включвайте релетата едно по едно от уеб страницата; след всяка промяна изчакайте ~3 s. VCC и A0 се четат от картите „Диагностика“ и „Измервания“.",
    "U_VIN — мултиметър между VIN и GND на платката на подчинения модул (точката, към която е свързан делителят). U_мод — VCC–GND на модула на реле 4. U_A0 — A0–GND.",
])
ws["A6"] = "Константи (от кода)"; ws["A6"].font = F_H2
consts = [("Пълна скала на A0 (A0 = 1024), V", 10.91, "0.000"),
          ("Корекция за 1 включено реле, V", 0.055, "0.000"),
          ("VCCOFFSET (уеб: „Корекция на VCC“), V", 0.0, "0.00")]
for i, (lbl, v, fmt) in enumerate(consts):
    rr = 7 + i
    cell(ws, f"A{rr}", lbl, "label")
    ws.merge_cells(f"A{rr}:D{rr}")
    cell(ws, f"E{rr}", v, "in" if i == 2 else "calc", fmt)
note(ws, "E7", "VCC_FULL_SCALE_V в Slave.ino (калибриран от Вас с мултиметър).")
note(ws, "E8", "VCC_RELAY_ERR_V в Slave.ino (калибриран от Вас с мултиметър).")
note(ws, "E9", "След качването на v3 е 0,00 V. Ако сте я променяли, впишете стойността от уеб страницата.")

ws["G6"] = "Делител към A0"; ws["G6"].font = F_H2
cell(ws, "G7", "R горен (VIN → A0), kΩ", "label"); ws.merge_cells("G7:I7"); cell(ws, "J7", None, "in", "0.00")
cell(ws, "G8", "R долен (A0 → GND), kΩ", "label"); ws.merge_cells("G8:I8"); cell(ws, "J8", None, "in", "0.00")
cell(ws, "G9", "Пълна скала по резисторите, V", "label"); ws.merge_cells("G9:I9")
cell(ws, "J9", '=IF(OR(J7="",J8=""),"",3.2*(J7+J8*320/(J8+320))/(J8*320/(J8+320)))', "calc", "0.00")
note(ws, "J9", "Предположение: вграденият делител на NodeMCU на A0 е 220 kΩ/100 kΩ (входно съпротивление 320 kΩ), "
               "а АЦП на ESP8266 има пълна скала 1,0 V, т.е. 3,2 V на извода A0 (типични стойности). "
               "Измерете резисторите с омметър при изключено захранване.")
cell(ws, "G10", "Разлика спрямо 10,91 V", "label"); ws.merge_cells("G10:I10")
cell(ws, "J10", '=IF(J9="","",J9/$E$7-1)', "calc", "0.0%")

example(ws, 12, ["Пример:", 2, 5.00, 4.95, 1.45, 5.00, 470.0], fmts=[None, "0", "0.00", "0.00", "0.000", "0.00", "0.0"])
heads = ["Серия", "Вкл. релета", "U_VIN (мултим.), V", "U_мод (мултим.), V", "U_A0 (мултим.), V",
         "VCC (уеб), V", "A0 средно (уеб)", "VCC без корекция за релетата, V",
         "Грешка с корекция (уеб − U_VIN), mV", "Грешка без корекция, mV",
         "Спад VIN → модул, mV", "U_A0 / U_VIN"]
header(ws, 13, heads)
first, last = 14, 28
for k in range(15):
    rr = first + k
    cell(ws, f"A{rr}", k // 5 + 1, "label", bold=True)
    cell(ws, f"B{rr}", k % 5, "label")
    for col, fmt in (("C", "0.00"), ("D", "0.00"), ("E", "0.000"), ("F", "0.00"), ("G", "0.0")):
        cell(ws, f"{col}{rr}", None, "in", fmt)
    cell(ws, f"H{rr}", f'=IF(G{rr}="","",G{rr}/1024*$E$7+$E$9)', "calc", "0.000")
    cell(ws, f"I{rr}", f'=IF(OR(F{rr}="",C{rr}=""),"",(F{rr}-C{rr})*1000)', "calc", "0")
    cell(ws, f"J{rr}", f'=IF(OR(H{rr}="",C{rr}=""),"",(H{rr}-C{rr})*1000)', "calc", "0")
    cell(ws, f"K{rr}", f'=IF(OR(C{rr}="",D{rr}=""),"",(C{rr}-D{rr})*1000)', "calc", "0")
    cell(ws, f"L{rr}", f'=IF(OR(E{rr}="",C{rr}=""),"",E{rr}/C{rr})', "calc", "0.0000")

ws["A30"] = "Обобщение по брой включени релета (средно от сериите)"; ws["A30"].font = F_H2
header(ws, 31, ["", "Вкл. релета", "U_VIN, V", "U_мод, V", "", "VCC (уеб), V", "",
                "", "Грешка с корекция, mV", "Грешка без корекция, mV", "Спад VIN → модул, mV", ""])
rng = lambda c: f"{c}${first}:{c}${last}"
for n in range(5):
    rr = 32 + n
    cell(ws, f"B{rr}", n, "label", bold=True)
    for col, fmt in (("C", "0.00"), ("D", "0.00"), ("F", "0.00"), ("I", "0"), ("J", "0"), ("K", "0")):
        cell(ws, f"{col}{rr}", f'=IFERROR(AVERAGEIFS({rng(col)},$B${first}:$B${last},$B{rr}),"")', "calc", fmt)
cell(ws, "H37", "Макс. |грешка|, mV", "label", bold=True)
for col in ("I", "J"):
    cell(ws, f"{col}37", f'=IF(COUNT({col}{first}:{col}{last})=0,"",MAX(MAX({col}{first}:{col}{last}),-MIN({col}{first}:{col}{last})))',
         "calc", "0", bold=True)

# ============================================================================
#  Е4 ИЗВОДИ
# ============================================================================
ws = sheet("Е4 Изводи", "Е4. Нива на изводите при рестарт и входът на релейния модул", [62, 14, 30])
text(ws, 3, ["Докато бутонът RST на подчинения модул е ЗАДЪРЖАН, изводите са входове — вижда се нивото, което се чете при стартиране."])
example(ws, 5, ["Пример:", 3.30], fmts=[None, "0.00"])
header(ws, 6, ["Измерване", "Стойност", "Бележка"])
items = ["D3–GND при задържан RST, модулът на реле 4 свързан към D3 (ако още не е преместен), V",
         "D3–GND при задържан RST, нищо не е свързано към D3, V",
         "D8–GND при задържан RST, модулът на реле 4 свързан към D8, V",
         "U(IN–GND) на релеен модул при ВКЛЮЧЕНО реле, V",
         "U(IN–GND) на релеен модул при ИЗКЛЮЧЕНО реле, V",
         "Ток в проводника към IN при включено реле, mA",
         "Положение на джъмперите H/L на четирите модула (снимка)"]
for i, it in enumerate(items):
    rr = 7 + i
    cell(ws, f"A{rr}", it, "label")
    ws[f"A{rr}"].alignment = Alignment(horizontal="left", vertical="center", wrap_text=True)
    cell(ws, f"B{rr}", None, "in", "0.00" if i < 6 else None)
    cell(ws, f"C{rr}", None, "in")

# ============================================================================
#  Е5 ТОК
# ============================================================================
ws = sheet("Е5 Ток", "Е5. Консумация на релейните модули", [34, 11, 11, 11, 11, 11])
text(ws, 3, ["Мултиметър в режим mA последователно в проводника +5 V към релейните модули (изключете захранването, преди да прекъснете веригата)."])
example(ws, 5, ["Пример:", 10.0, 80.0], fmts=[None, "0.0", "0.0"])
header(ws, 6, ["Вкл. релета", 0, 1, 2, 3, 4])
cell(ws, "A7", "Ток, mA", "label", bold=True)
cell(ws, "A8", "Прираст спрямо предходното, mA", "label")
for j, col in enumerate("BCDEF"):
    cell(ws, f"{col}7", None, "in", "0.0")
    if j:
        prev = "BCDEF"[j - 1]
        cell(ws, f"{col}8", f'=IF(OR({col}7="",{prev}7=""),"",{col}7-{prev}7)', "calc", "0.0")
    else:
        cell(ws, f"{col}8", "—", "calc")
cell(ws, "A10", "Среден ток на едно реле, mA", "label", bold=True)
cell(ws, "B10", '=IF(OR(B7="",F7=""),"",(F7-B7)/4)', "calc", "0.0", bold=True)
cell(ws, "A11", "Напрежение на модулите при 4 релета, V (от Е3)", "label")
cell(ws, "B11", None, "in", "0.00")
cell(ws, "A12", "Мощност на релейните модули при 4 релета, W", "label", bold=True)
cell(ws, "B12", '=IF(OR(B11="",F7=""),"",B11*F7/1000)', "calc", "0.00", bold=True)

# ============================================================================
#  Е6 БУТОНИ
# ============================================================================
ws = sheet("Е6 Бутони", "Е6. Бутони и програмният филтър срещу трептене", [24, 13, 13, 13, 15, 16, 18, 16])
text(ws, 3, [
    "RESET STATS → натиснете бутона точно толкова пъти, колкото е в колоната „Натиснат“ → препишете от „Бутон D5: натиск. / фронтове / макс.“ (три числа).",
    "D6 изключва всички релета — това е нормално. Прекъсването може да не хване импулси, по-кратки от няколко µs — броят фронтове е долна граница.",
])
example(ws, 6, ["Пример:", 50, 50, 120, 5])
header(ws, 7, ["Бутон", "Натиснат, пъти", "Приети натискания", "Фронтове общо", "Макс. фронтове за 1 превключване",
               "Приети − натиснати", "Фронтове над 2 на натискане (трептене)", "Средно фронтове на натискане"])
for i, (lbl, n) in enumerate([("D5", 50), ("D6", 50), ("D5 — кратки почуквания", 20)]):
    rr = 8 + i
    cell(ws, f"A{rr}", lbl, "label", bold=True)
    cell(ws, f"B{rr}", n, "in", "0")
    for col in "CDE":
        cell(ws, f"{col}{rr}", None, "in", "0")
    cell(ws, f"F{rr}", f'=IF(OR(B{rr}="",C{rr}=""),"",C{rr}-B{rr})', "calc", "0")
    cell(ws, f"G{rr}", f'=IF(OR(B{rr}="",D{rr}=""),"",D{rr}-2*B{rr})', "calc", "0")
    cell(ws, f"H{rr}", f'=IF(OR(B{rr}="",B{rr}=0,D{rr}=""),"",D{rr}/B{rr})', "calc", "0.00")

# ============================================================================
#  Е7 ВЛАГА
# ============================================================================
ws = sheet("Е7 Влага", "Е7. Защита от влага (HR202 + компаратор)", [12, 15, 16, 15, 16, 14, 20, 26])
text(ws, 3, ["Ако DO при сухо е около 5 V, модулът трябва да се захранва от 3V3 (входовете на ESP8266 са за 3,3 V) — пишете ми преди да продължите."])
cell(ws, "A5", "Захранване на модула HR202, V (3,3 или 5?)", "label"); ws.merge_cells("A5:D5"); cell(ws, "E5", None, "in", "0.00")
cell(ws, "A6", "Изход DO–GND при СУХО, V", "label"); ws.merge_cells("A6:D6"); cell(ws, "E6", None, "in", "0.00")
cell(ws, "A7", "Изход DO–GND при МОКРО, V", "label"); ws.merge_cells("A7:D7"); cell(ws, "E7", None, "in", "0.00")
text(ws, 9, ["5 опита: включете всички релета → намокрете датчика → проверете → подсушете. Попълнете с Да / Не (падащ списък)."])
example(ws, 10, ["Пример:", "Да", "Да", "Да", "Да", "Да", "Да", ""])
header(ws, 11, ["Опит", "Всички релета изключени?", "Уеб: „МОКРО (БЛОКИРАНО)“?", "Отказ от уеб?",
                "Отказ по UART (R1 ON на главния)?", "Отказ от бутон D5?",
                "След изсъхване: „СУХО (ИЗЧАКВАНЕ)“ → „СУХО“?", "Бележка"])
for i in range(5):
    rr = 12 + i
    cell(ws, f"A{rr}", i + 1, "label", bold=True)
    for col in "BCDEFGH":
        cell(ws, f"{col}{rr}", None, "in")
yes_no_ranges.append((ws, "B12:G16"))
cell(ws, "A17", "Брой „Да“", "label", bold=True)
for col in "BCDEFG":
    cell(ws, f"{col}17", f'=COUNTIF({col}12:{col}16,"Да")&" от "&COUNTA({col}12:{col}16)', "calc", bold=True)
cell(ws, "A19", "След опитите — „Защита: задействания / откази“ от уеб страницата:", "label"); ws.merge_cells("A19:D19")
cell(ws, "E19", None, "in", "0"); cell(ws, "F19", None, "in", "0")
note(ws, "E19", "задействания"); note(ws, "F19", "откази")

# ============================================================================
#  Е8 PING
# ============================================================================
ws = sheet("Е8 PING", "Е8. Закъснение (PING) и потвърждение на ESP-NOW",
           [8, 20, 11, 11, 11, 12, 12, 12, 13, 13, 13, 12, 12, 13, 30])
text(ws, 3, [
    "Модулите са на 1–2 m. На главния (Serial Monitor или PuTTY): RESET STATS → PING 100 → изчакайте ~30 s до „📶 Край на теста“.",
    "От последните два реда: „Край на теста: изпратени A, успешни B, загубени C“ и „RTT мин / ср / макс: X / Y / Z µs“ → колони C–H.",
    "След това въведете 7 и препишете реда „ESP-NOW потвърждение: N бр., мин./ср./макс. P / Q / R µs, неуспешни F“ → колони I–L.",
    "Уеб страницата показва същото в ms (напр. 6.58 ms = 6580 µs) — тук пишете в µs.",
])
example(ws, 8, ["Пример:", "1–2 m", 100, 100, 0, 5000, 7000, 30000, 1500, 2000, 3500, 0, "", "", "ping_1.log"])
header(ws, 9, ["Серия", "Условия", "Изпратени", "Успешни", "Загубени", "RTT мин., µs", "RTT ср., µs", "RTT макс., µs",
               "ESP-NOW потв. мин., µs", "ESP-NOW потв. ср., µs", "ESP-NOW потв. макс., µs", "Неуспешни (MAC)",
               "Доставени, %", "RTT ср., ms", "Файл със записа"])
conds = ["1–2 m", "1–2 m", "1–2 m", "1–2 m, 3 уеб клиента"]
for i, cnd in enumerate(conds):
    rr = 10 + i
    cell(ws, f"A{rr}", i + 1, "label", bold=True)
    cell(ws, f"B{rr}", cnd, "label")
    for col in "CDEFGHIJKL":
        cell(ws, f"{col}{rr}", None, "in", "0")
    cell(ws, f"M{rr}", f'=IF(OR(D{rr}="",E{rr}=""),"",IF(D{rr}+E{rr}=0,"",D{rr}/(D{rr}+E{rr})))', "calc", "0.0%")
    cell(ws, f"N{rr}", f'=IF(G{rr}="","",G{rr}/1000)', "calc", "0.00")
    cell(ws, f"O{rr}", None, "in")
cell(ws, "B15", "Серии 1–3 общо", "label", bold=True)
cell(ws, "D15", '=IF(COUNT(D10:D12)=0,"",SUM(D10:D12))', "calc", "0", bold=True)
cell(ws, "E15", '=IF(COUNT(E10:E12)=0,"",SUM(E10:E12))', "calc", "0", bold=True)
cell(ws, "F15", '=IF(COUNT(F10:F12)=0,"",MIN(F10:F12))', "calc", "0", bold=True)
cell(ws, "G15", '=IF(COUNT(G10:G12)=0,"",AVERAGE(G10:G12))', "calc", "0", bold=True)
cell(ws, "H15", '=IF(COUNT(H10:H12)=0,"",MAX(H10:H12))', "calc", "0", bold=True)
cell(ws, "M15", '=IF(OR(D15="",E15=""),"",IF(D15+E15=0,"",D15/(D15+E15)))', "calc", "0.0%", bold=True)
note(ws, "G15", "Средно от средните на трите серии (всяка с по 100 заявки).")

# ============================================================================
#  Е9 НАДЕЖДНОСТ
# ============================================================================
ws = sheet("Е9 Надеждност", "Е9. Надеждност на връзката (поне 1 час)",
           [18, 18, 11, 11, 13, 13, 12, 12, 13, 14, 14, 30])
text(ws, 3, [
    "RESET STATS → LOG ON със запис във файл → оставете без намеса. В края препишете от уеб картата „⏱️ Измервания“ и „📊 Диагностика“ (или от командата 7).",
    "„Приети / загубени / доставени“ → колони C–D; „Качество на връзката“ и „Загубени пакети“ се отнасят за MAC ниво → колони E–F (успешни = пакети, потвърдени от главния).",
])
example(ws, 6, ["Пример:", "25.09.2026 22:00", "26.09.2026 07:30", 17000, 0, 17000, 0, 0])
header(ws, 7, ["Начало (дата и час)", "Край (дата и час)", "Приети", "Загубени", "Успешни (MAC)", "Неуспешни (MAC)",
               "Рестарти на подч. модул", "Продълж., h", "Доставени, %", "Успешни (MAC), %",
               "Очаквани пакети (по 1 на 2 s)", "Файл със записа"])
for i in range(3):
    rr = 8 + i
    cell(ws, f"A{rr}", None, "in", "dd.mm.yyyy hh:mm")
    cell(ws, f"B{rr}", None, "in", "dd.mm.yyyy hh:mm")
    for col in "CDEFG":
        cell(ws, f"{col}{rr}", None, "in", "0")
    cell(ws, f"H{rr}", f'=IF(OR(A{rr}="",B{rr}=""),"",(B{rr}-A{rr})*24)', "calc", "0.00")
    cell(ws, f"I{rr}", f'=IF(OR(C{rr}="",D{rr}=""),"",IF(C{rr}+D{rr}=0,"",C{rr}/(C{rr}+D{rr})))', "calc", "0.000%")
    cell(ws, f"J{rr}", f'=IF(OR(E{rr}="",F{rr}=""),"",IF(E{rr}+F{rr}=0,"",E{rr}/(E{rr}+F{rr})))', "calc", "0.000%")
    cell(ws, f"K{rr}", f'=IF(H{rr}="","",H{rr}*1800)', "calc", "0")
    cell(ws, f"L{rr}", None, "in")
note(ws, "K8", "Само периодичните пакети при интервал 2000 ms; незабавните отчети след команди и бутони ги увеличават.")

# ============================================================================
#  Е10 ОБХВАТ
# ============================================================================
ws = sheet("Е10 Обхват", "Е10. Обхват (по желание)", [26, 14, 11, 11, 11, 13, 13, 13])
text(ws, 3, [
    "Подчиненият модул — от power bank. На всяко място: RESET STATS → PING 100 → препишете последните два реда.",
    "Ако опитът не се направи, в дипломната работа ще пише, че обхватът не е изследван експериментално (само теоретична оценка).",
])
example(ws, 6, ["Пример:", 10, 1, 95, 5, 8000, 40000], fmts=[None, "0.0"])
header(ws, 7, ["Място (описание)", "Разстояние, m", "Стени", "Успешни", "Загубени", "RTT ср., µs", "RTT макс., µs", "Доставени, %"])
for i, (d, w) in enumerate([(1, 0), (5, 0), (10, 0), (None, 1), (None, 2), (None, None)]):
    rr = 8 + i
    cell(ws, f"A{rr}", None, "in")
    cell(ws, f"B{rr}", d, "in", "0.0")
    cell(ws, f"C{rr}", w, "in", "0")
    for col in "DEFG":
        cell(ws, f"{col}{rr}", None, "in", "0")
    cell(ws, f"H{rr}", f'=IF(OR(D{rr}="",E{rr}=""),"",IF(D{rr}+E{rr}=0,"",D{rr}/(D{rr}+E{rr})))', "calc", "0.0%")

# ============================================================================
#  Е11 24 ЧАСА
# ============================================================================
ws = sheet("Е11 24 часа", "Е11. Дълготраен тест (24 h, по желание)", [40, 16, 16])
text(ws, 3, ["RESET STATS → LOG ON със запис → 24 h без намеса → команда 7 на главния + снимка на уеб страницата."])
example(ws, 5, ["Пример:", 86400, 86400])
header(ws, 6, ["Показател", "Главен", "Подчинен"])
for i, lbl in enumerate(["Време на работа, s", "Мин. свободна памет, B", "Фрагментация, %",
                         "Макс. време за цикъл от старта, µs", "Причина за рестарт",
                         "Грешки от DHT11", "Загубени пакети", "Доставени, %"]):
    rr = 7 + i
    cell(ws, f"A{rr}", lbl, "label")
    ws[f"A{rr}"].alignment = Alignment(horizontal="left", vertical="center")
    cell(ws, f"B{rr}", "—" if lbl == "Грешки от DHT11" else None, "label" if lbl == "Грешки от DHT11" else "in")
    cell(ws, f"C{rr}", None, "in")

# ============================================================================
#  Е12 ВКЛЮЧВАНЕ
# ============================================================================
ws = sheet("Е12 Включване", "Е12. Поведение при подаване на захранване", [46, 14, 12, 30])
text(ws, 3, ["10 пъти изключете и включете USB на подчинения модул; бройте."])
example(ws, 5, ["Пример:", 10])
header(ws, 6, ["Наблюдение", "Брой от 10", "%", "Бележка"])
for i, lbl in enumerate(["Платката стартира нормално", "Реле 1 (D0) щраква кратко при включване",
                         "Реле 4 (D8) щраква при включване"]):
    rr = 7 + i
    cell(ws, f"A{rr}", lbl, "label")
    ws[f"A{rr}"].alignment = Alignment(horizontal="left", vertical="center")
    cell(ws, f"B{rr}", None, "in", "0")
    cell(ws, f"C{rr}", f'=IF(B{rr}="","",B{rr}/10)', "calc", "0%")
    cell(ws, f"D{rr}", None, "in")

# ============================================================================
#  Е13 УЕБ
# ============================================================================
ws = sheet("Е13 Уеб", "Е13. Време за отговор на уеб сървъра (по желание)", [10, 18, 18])
text(ws, 3, ["Chrome на компютъра: F12 → Network → заявка data → Timing → „Waiting for server response“, ms. 20 поредни заявки."])
example(ws, 5, ["Пример:", 20.0, 25.0], fmts=[None, "0.0", "0.0"])
header(ws, 6, ["№", "1 клиент, ms", "3 клиента, ms"])
for i in range(20):
    rr = 7 + i
    cell(ws, f"A{rr}", i + 1, "label")
    cell(ws, f"B{rr}", None, "in", "0.0")
    cell(ws, f"C{rr}", None, "in", "0.0")
for j, (lbl, fn) in enumerate([("Средно", "AVERAGE"), ("Мин.", "MIN"), ("Макс.", "MAX")]):
    rr = 28 + j
    cell(ws, f"A{rr}", lbl, "label", bold=True)
    for col in "BC":
        cell(ws, f"{col}{rr}", f'=IF(COUNT({col}7:{col}26)=0,"",{fn}({col}7:{col}26))', "calc", "0.0", bold=True)

# ============================================================================
#  СНИМКИ
# ============================================================================
ws = sheet("Снимки", "Снимки и екранни снимки", [70, 12, 30])
header(ws, 3, ["Какво", "Направено?", "Име на файла"])
shots = ["Целият макет отгоре, добре осветен",
         "NodeMCU на подчинения модул отблизо — с надписите на изводите",
         "Релейните модули отблизо — джъмперът H",
         "Модулът HR202 с потенциометъра",
         "DHT11", "Двата бутона", "Делителят към A0", "Главният модул",
         "Уеб: горната част (датчици и релета)", "Уеб: „📊 Диагностика“", "Уеб: „⏱️ Измервания“",
         "Уеб: „⚙️ Конфигурация“", "UART: менюто M на двата модула", "UART: 7 на главния",
         "UART: 6 на подчинения", "UART: резултат от PING 100"]
for i, s in enumerate(shots):
    rr = 4 + i
    cell(ws, f"A{rr}", s, "label")
    ws[f"A{rr}"].alignment = Alignment(horizontal="left", vertical="center")
    cell(ws, f"B{rr}", None, "in")
    cell(ws, f"C{rr}", None, "in")
yes_no_ranges.append((ws, f"B4:B{3 + len(shots)}"))

# ---------------------------------------------------------------- Да/Не -----
for w, rng_ in yes_no_ranges:
    dv = DataValidation(type="list", formula1='"Да,Не"', allow_blank=True)
    dv.error = "Изберете Да или Не"
    dv.errorTitle = "Невалидна стойност"
    w.add_data_validation(dv)
    dv.add(rng_)

wb.calculation.fullCalcOnLoad = True
wb.save(OUT)
print("saved", OUT, [s.title for s in wb.worksheets])

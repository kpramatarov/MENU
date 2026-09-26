#!/usr/bin/env python3
"""Сглобява дипломната работа в един Word документ и я изобразява в PDF за проверка.

Употреба:  python3 sglobyavane.py      (от папката diplomna_rabota)
Изисква:   pandoc; за точните номера на страниците в съдържанието – LibreOffice (soffice)
           и pypdfium2. Без тях номерата се оценяват приблизително.

Редът на частите следва методическите указания на катедра „Електронна техника“:
заглавна страница, задание, декларация, съдържание, списък на съкращенията, увод, глави,
заключение, литература, анотация (1 страница), приложения.
Номерата на страниците в съдържанието се вземат от изобразения документ: сглобяване,
изобразяване, търсене на заглавията по страниците, повторно сглобяване.
"""
import os
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

OUT = 'Diplomna_rabota_Pramatarov.docx'
PDF = 'Diplomna_rabota_Pramatarov.pdf'
PB = '\n```{=openxml}\n<w:p><w:r><w:br w:type="page"/></w:r></w:p>\n```\n\n'
WIDE = ('principna', 'blokova', 'uart', 'simulacia', 'ping', 'histogram', 'master', 'slave', 'releen',
        'izmervane', 'struktura', 'opashka', 'posledovatelnost', 'telemetria', 'zashtita')
NARROW = {'maket': '7.5cm'}        # снимки и високи фигури

CHAPTERS = [('Uvod.md', 'УВОД'),
            ('Glava_1_Literaturno_prouchvane.md', 'ГЛАВА ПЪРВА. ЛИТЕРАТУРНО ПРОУЧВАНЕ'),
            ('Glava_2_Harduerno_proektirane.md', 'ГЛАВА ВТОРА. ХАРДУЕРНО ПРОЕКТИРАНЕ'),
            ('Glava_3_Softuerno_proektirane.md', 'ГЛАВА ТРЕТА. СОФТУЕРНО ПРОЕКТИРАНЕ'),
            ('Glava_4_Eksperimentalni_rezultati.md',
             'ГЛАВА ЧЕТВЪРТА. ЕКСПЕРИМЕНТАЛНИ И СИМУЛАЦИОННИ ИЗСЛЕДВАНИЯ'),
            ('Zaklyuchenie.md', 'ЗАКЛЮЧЕНИЕ')]
SHEETS = [('List_1_Blokova_shema', 'Лист 1. Блокова схема на хардуера (т. 5.1 от заданието)'),
          ('List_2_Principna_shema', 'Лист 2. Принципна електрическа схема на подчинения възел (т. 5.2)'),
          ('List_3_Algoritam', 'Лист 3. Алгоритъм на управляващата програма (т. 5.3)')]
APP_A = 'ПРИЛОЖЕНИЕ А. Графична част'


# ------------------------------------------------------------------ OpenXML --
def esc(t):
    return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def raw(xml):
    return '\n```{=openxml}\n' + xml + '\n```\n'


def para(runs, style=None, before=None, after=None):
    """Абзац с даден стил; runs - низ или списък от (текст, удебелен); '\t' е табулация."""
    if isinstance(runs, str):
        runs = [(runs, False)]
    ppr = f'<w:pStyle w:val="{style}"/>' if style else ''
    if before is not None or after is not None:
        ppr += '<w:spacing' + (f' w:before="{before}"' if before is not None else '') + \
               (f' w:after="{after}"' if after is not None else '') + '/>'
    out = f'<w:p><w:pPr>{ppr}</w:pPr>'
    for text, bold in runs:
        rpr = '<w:rPr><w:b/><w:bCs/></w:rPr>' if bold else ''
        for k, part in enumerate(text.split('\t')):
            if k:
                out += f'<w:r>{rpr}<w:tab/></w:r>'
            if part:
                out += f'<w:r>{rpr}<w:t xml:space="preserve">{esc(part)}</w:t></w:r>'
    return out + '</w:p>'


def title_page():
    info = [('Дипломант:', 'инж. Кръстиян Тодоров Праматаров'), ('Факултетен номер:', '901322003'),
            ('Специалност:', 'Електронни системи за хибридни и електромобили'),
            ('Форма на обучение:', 'редовна'), ('Научен ръководител:', 'доц. д-р инж. Любомир Богданов'),
            ('Консултант:', '..............................................')]
    x = para('ТЕХНИЧЕСКИ УНИВЕРСИТЕТ – СОФИЯ', 'CenterMed', before=0)
    x += para('ФАКУЛТЕТ ПО ЕЛЕКТРОННА ТЕХНИКА И ТЕХНОЛОГИИ', 'CenterSml')
    x += para('Катедра „Електронна техника“', 'CenterSml')
    x += para('ДИПЛОМНА РАБОТА', 'CenterBig', before=1700)
    x += para('за придобиване на образователно-квалификационна степен „магистър“', 'CenterSml')
    x += para('на тема:', 'CenterSml', before=360)
    x += para('СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС', 'CenterMed')
    x += para([(info[0][0] + '\t', True), (info[0][1], False)], 'TitleInfo', before=1100)
    for k, v in info[1:]:
        x += para([(k + '\t', True), (v, False)], 'TitleInfo')
    x += para('Дипломант: ...........................', 'TitleInfo', before=900)
    x += para('Научен ръководител: ...........................', 'TitleInfo', before=360)
    x += para('София, 2026 г.', 'CenterMed', before=1500)
    return raw(x)


def assignment_page():
    x = para('ЗАДАНИЕ ЗА ДИПЛОМНА РАБОТА', 'CenterMed', before=0)
    x += para('Тук се поставя утвърденото задание с подписи и печат (сканирано копие).', 'CenterSml',
              before=480)
    return raw(x)


def toc(entries, pages):
    """Съдържание: заглавие, точки и номер на страницата (стилове TOC1 и TOC2)."""
    x = ''.join(para(f'{title}\t{pg}', 'TOC1' if lvl == 1 else 'TOC2')
                for (title, lvl, _), pg in zip(entries, pages))
    return '# СЪДЪРЖАНИЕ\n' + raw(x)


# ------------------------------------------------- номера на страниците --
def toc_entries():
    """(заглавие, ниво, ключ за търсене) за всички части, изброени в съдържанието."""
    e = [('СПИСЪК НА ИЗПОЛЗВАНИТЕ СЪКРАЩЕНИЯ', 1, 'СПИСЪК НА ИЗПОЛЗВАНИТЕ СЪКРАЩЕНИЯ')]
    for f, title in CHAPTERS:
        e.append((title, 1, title.split('. ')[0]))
        for m in re.finditer(r'^## (\d+\.\d+\.) (.+)$', open(f, encoding='utf-8').read(), re.M):
            e.append((f'{m.group(1)} {m.group(2).strip()}', 2, m.group(1) + ' ' + m.group(2).split()[0]))
    e += [('ИЗПОЛЗВАНА ЛИТЕРАТУРА', 1, 'ИЗПОЛЗВАНА ЛИТЕРАТУРА'), ('АНОТАЦИЯ', 1, 'АНОТАЦИЯ'),
          (APP_A, 1, 'ПРИЛОЖЕНИЕ А'), ('ПРИЛОЖЕНИЕ Б. Програмно осигуряване', 1, 'ПРИЛОЖЕНИЕ Б')]
    return e


def find_pages(pdf_path, entries):
    """Страниците на заглавията в изобразения документ (последователно търсене след съдържанието)."""
    import pypdfium2 as pdfium
    doc = pdfium.PdfDocument(pdf_path)
    lines = []
    for i in range(len(doc)):
        t = doc[i].get_textpage().get_text_range().replace('\r', '')
        lines.append([l.strip() for l in t.split('\n') if l.strip()])
    p = next(i for i, ls in enumerate(lines) if 'СЪДЪРЖАНИЕ' in ls) + 1
    pages = []
    for title, lvl, key in entries:
        # частите от първо ниво започват на нова страница - заглавието е в първите редове;
        # търсенето тръгва след съдържанието, затова редовете му не се бъркат със заглавията
        def found(ls):
            return any(l == key or l.startswith(key + ' ') or l.startswith(key + '.')
                       for l in (ls[:3] if lvl == 1 else ls))
        while p < len(lines) and not found(lines[p]):
            p += 1
        if p >= len(lines):
            raise RuntimeError('не е намерено заглавие: ' + title)
        pages.append(p + 1)
    return pages, len(doc)


def render_pdf(docx, outdir):
    """Изобразява docx в PDF с LibreOffice; връща пътя или None."""
    soffice = shutil.which('soffice') or shutil.which('libreoffice')
    if not soffice:
        return None
    with tempfile.TemporaryDirectory(prefix='lo_profile_') as prof:
        r = subprocess.run([soffice, f'-env:UserInstallation={Path(prof).as_uri()}', '--headless',
                            '--convert-to', 'pdf', '--outdir', outdir, docx], capture_output=True, text=True,
                           timeout=600, env=dict(os.environ, SAL_USE_VCLPLUGIN='svp'))
    pdf = os.path.join(outdir, Path(docx).stem + '.pdf')
    return pdf if r.returncode == 0 and os.path.exists(pdf) else None


def est_pages(t):
    """Груба оценка на броя страници (A4, 14 pt, около 2000 знака на страница)."""
    pr, tr, hd, fm, im, code = [], 0, 0, 0, 0, 0
    inc = False
    for l in t.split('\n'):
        x = l.strip()
        if x.startswith('```'):
            inc = not inc
            continue
        if inc:
            code += 1
        elif x.startswith('|'):
            tr += 1
        elif x.startswith('!['):
            im += 1
        elif x.startswith('#'):
            hd += 1
        elif x.startswith('$$'):
            fm += 1
        elif x not in ('', '---'):
            pr.append(re.sub(r'[*`>]', '', x))
    return len(' '.join(pr)) / 2000 + tr * .04 + hd * .05 + fm * .06 + im * .4 + code * .02


def estimated_pages(entries):
    """Приблизителни номера на страниците, когато документът не може да бъде изобразен."""
    est, p = {}, 8.0
    for f, title in CHAPTERS:
        t = open(f, encoding='utf-8').read()
        est[title] = p
        acc = p
        for sec in re.split(r'\n(?=## \d+\.\d+\.)', t)[1:]:
            est[sec.split('\n')[0][3:].strip()] = acc
            acc += est_pages(sec)
        p += est_pages(t)
    est['ИЗПОЛЗВАНА ЛИТЕРАТУРА'] = p
    est['АНОТАЦИЯ'] = p + 3
    est[APP_A] = p + 4
    est['ПРИЛОЖЕНИЕ Б. Програмно осигуряване'] = p + 8
    out = []
    for title, _, _ in entries:
        out.append(str(round(est.get(title, est.get(title.split(' ', 1)[-1], 6)))))
    return out


# ------------------------------------------------------------ сглобяване --
def load(p):
    """Чете част от работата: SVG -> PNG с подходяща ширина, без хоризонталните линии."""
    t = open(p, encoding='utf-8').read()

    def img(m):
        alt, src = m.group(1), m.group(2)
        png = src.replace('figuri/', 'figuri/png/').replace('.svg', '.png')
        w = next((v for k, v in NARROW.items() if k in png), None) or \
            ('15.5cm' if any(k in png for k in WIDE) else '13cm')
        return '![{}]({}){{width={}}}'.format(alt, png, w)
    t = re.sub(r'!\[([^\]]*)\]\(([^)]+\.svg)\)', img, t)
    t = re.sub(r'(?m)^---\s*$\n?', '', t)
    return t.strip()


def appendix_a(tmp):
    """Листовете A1 са завъртени на 90°, за да заемат цялата страница A4."""
    from PIL import Image
    out = ['# ПРИЛОЖЕНИЕ А', '', '## Графична част – листове 1…3', '',
           'Листовете са изпълнени във формат A1 (841 × 594 mm) с основен надпис по ЕСКД. Тук са '
           'дадени в умален вид, завъртени на 90°; оригиналите са приложени отделно.']
    for f, cap in SHEETS:
        rot = os.path.join(tmp, f + '_A4.png')
        if not os.path.exists(rot):
            Image.open(f'grafichna_chast/{f}.png').rotate(90, expand=True).save(rot)
        out += [PB, f'![{cap}]({rot}){{width=15.5cm}}', '']
    return '\n'.join(out)


def update_annotation(pages_main):
    """Обновява бройките в анотацията от действителното съдържание."""
    allt = '\n'.join(open(f, encoding='utf-8').read() for f, _ in CHAPTERS)
    figs = len(set(re.findall(r'!\[Фиг\. (\d+\.\d+)\]', allt)))
    tabs = len(re.findall(r'\*\*Таблица \d+\.\d+\.', allt))
    app_b = open('Prilozhenie_B_programi.md', encoding='utf-8').read()
    lsts = len(re.findall(r'\*\*Листинг [А-Я]\.\d+\.', app_b))
    tabs_b = len(re.findall(r'\*\*Таблица [А-Я]\.\d+\.', app_b))
    refs = len(re.findall(r'^\d+\. ', open('Literatura.md', encoding='utf-8').read(), re.M))
    a = open('Anotaciya.md', encoding='utf-8').read()
    a = re.sub(r'Обемът на дипломната работа е \d+ страници(?:.|\n(?!\n))*',
               f'Обемът на дипломната работа е {pages_main} страници без приложенията. Тя съдържа '
               f'{figs} фигури,\n{tabs} таблици и {refs} литературни източника. Приложенията съдържат '
               f'графичната част\n(3 листа формат A1), {tabs_b} таблици и {lsts} листинга от програмите.\n', a)
    open('Anotaciya.md', 'w', encoding='utf-8').write(a)
    return figs, tabs, lsts, refs


TEXT_W, CHAR_W, CELL_PAD = 8787, 108, 190    # twips: ширина на текста, знак при 12 pt, полета на клетката


def fit_table(tbl):
    """Ширини на колоните по съдържанието: най-малко - най-дългата дума; остатъкът се дава там,
    където намалява най-много общият брой редове на таблицата."""
    rows = re.findall(r'<w:tr[ >].*?</w:tr>', tbl, re.S)

    def cell_text(c):
        """Текстът на клетката; думите в код (моноширинен шрифт) се удължават с 35 %,
        защото знаците им са толкова по-широки."""
        out = ''
        for run in re.findall(r'<w:r>.*?</w:r>|<m:r>.*?</m:r>', c, re.S):
            t = ''.join(re.findall(r'<(?:w|m):t(?: [^>]*)?>([^<]*)</(?:w|m):t>', run))
            if 'VerbatimChar' in run:
                t = ' '.join(w + '_' * round(len(w) * 0.35) for w in t.split(' '))
            out += t
        return out
    cells = [[cell_text(c) for c in re.findall(r'<w:tc>.*?</w:tc>', r, re.S)] for r in rows]
    if cells:                   # заглавният ред е удебелен - около 12 % по-широк
        cells[0] = [' '.join(w + '_' * round(len(w) * 0.12) for w in c.split(' ')) for c in cells[0]]
    n = len(cells[0]) if cells else 0
    if n < 2 or any(len(r) != n for r in cells):
        return tbl
    cap = lambda w: max(1, int((w - CELL_PAD) / CHAR_W))

    def height(ws):             # непрекъсната оценка: редът е висок колкото най-запълнената клетка
        return sum(max(len(c) / cap(w) for c, w in zip(r, ws)) for r in cells)
    ws = [CELL_PAD + CHAR_W * max(3, max(max((len(x) for x in c.split()), default=1) for c in col))
          for col in zip(*cells)]
    if sum(ws) > TEXT_W:                       # твърде дълги думи - пропорционално свиване
        ws = [w * TEXT_W // sum(ws) for w in ws]
    step = CHAR_W
    while sum(ws) + step <= TEXT_W:
        h0 = height(ws)
        best = min(range(n), key=lambda j: (height(ws[:j] + [ws[j] + step] + ws[j + 1:]) - h0, ws[j]))
        ws[best] += step
    ws[-1] += TEXT_W - sum(ws)
    grid = '<w:tblGrid>' + ''.join(f'<w:gridCol w:w="{w}" />' for w in ws) + '</w:tblGrid>'
    tbl = re.sub(r'<w:tblGrid>.*?</w:tblGrid>', grid, tbl, count=1, flags=re.S)
    tbl = re.sub(r'<w:tblW [^>]*/>', f'<w:tblW w:type="dxa" w:w="{TEXT_W}" /><w:tblLayout w:type="fixed" />', tbl, 1)

    def row(m):
        k = iter(ws)
        return re.sub(r'<w:tc><w:tcPr\s*/>|<w:tc><w:tcPr>',
                      lambda c: f'<w:tc><w:tcPr><w:tcW w:w="{next(k)}" w:type="dxa" />' +
                      ('</w:tcPr>' if c.group(0).endswith('/>') else ''), m.group(0))
    return re.sub(r'<w:tr[ >].*?</w:tr>', row, tbl, flags=re.S)


PB_XML = '<w:p><w:r><w:br w:type="page"/></w:r></w:p>'


def page_break_before(doc):
    """Празният абзац с нова страница се заменя със „започни от нова страница“ на следващия абзац -
    така не се получава празна страница, когато предходната част запълва страницата до края."""
    parts = doc.split(PB_XML)
    out = parts[0]
    for part in parts[1:]:
        s = part.lstrip()
        if not s.startswith('<w:p>'):
            out += PB_XML + part           # следва таблица - оставя се обикновеният разделител
            continue
        m = re.match(r'<w:p><w:pPr>((?:<w:pStyle [^>]*/>)?(?:<w:keepNext\s*/>)?(?:<w:keepLines\s*/>)?)', s)
        if m:
            s = s[:m.end()] + '<w:pageBreakBefore />' + s[m.end():]
        else:
            s = '<w:p><w:pPr><w:pageBreakBefore /></w:pPr>' + s[len('<w:p>'):]
        out += s
    return out


def postprocess(path):
    """Таблиците: собствен стил на текста (12 pt, единично междуредие) и ширини по съдържанието;
    нова страница без празни абзаци; без повторените надписи „Фиг. N.M“ (те са в самите фигури)."""
    z = zipfile.ZipFile(path)
    files = {n: z.read(n) for n in z.namelist()}
    z.close()
    doc = files['word/document.xml'].decode('utf-8')
    doc = re.sub(r'<w:tbl>.*?</w:tbl>',
                 lambda m: fit_table(re.sub(r'<w:pStyle w:val="Compact"\s*/>', '<w:pStyle w:val="TableText" />',
                                            m.group(0))),
                 doc, flags=re.S)
    doc = re.sub(r'<w:p><w:pPr><w:pStyle w:val="ImageCaption" />(?:(?!</w:p>).)*?<w:t[^>]*>Фиг\. \d+\.\d+</w:t>'
                 r'(?:(?!<w:p>).)*?</w:p>', '', doc, flags=re.S)
    # кодът в листингите - с размера на стила на абзаца (8,5 pt), без оцветяване
    doc = re.sub(r'<w:p><w:pPr><w:pStyle w:val="SourceCode" />.*?</w:p>',
                 lambda m: re.sub(r'<w:rPr><w:rStyle w:val="[A-Za-z]+Tok" /></w:rPr>', '', m.group(0)),
                 doc, flags=re.S)
    doc = page_break_before(doc)
    files['word/document.xml'] = doc.encode('utf-8')
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as out:
        for n, data in files.items():
            out.writestr(n, data)
    shutil.move(tmp, path)


def build(tmp, entries, pages):
    parts = [title_page(), PB, assignment_page(), PB, load('Zaglavna_chast.md'), PB,
             toc(entries, pages), PB, load('Sakrashteniya.md')]
    for f, _ in CHAPTERS:
        parts += [PB, load(f)]
    parts += [PB, load('Literatura.md'), PB, load('Anotaciya.md'), PB, appendix_a(tmp), PB,
              load('Prilozhenie_B_programi.md')]
    open('_sglobyavane_izhoden.md', 'w', encoding='utf-8').write('\n\n'.join(parts))
    subprocess.run(['pandoc', '_sglobyavane_izhoden.md', '--reference-doc=_sglobyavane_stilove.docx',
                    '--resource-path=.', '-f', 'markdown+tex_math_dollars+pipe_tables+fenced_divs+raw_attribute',
                    '-t', 'docx', '-o', OUT], check=True)
    postprocess(OUT)


def main():
    entries = toc_entries()
    with tempfile.TemporaryDirectory() as tmp:
        pages, done = ['0'] * len(entries), False
        for _ in range(4):                       # номерата се установяват след 2-3 прохода
            build(tmp, entries, pages)
            pdf = render_pdf(OUT, tmp)
            if pdf is None:
                break
            try:
                new, total = find_pages(pdf, entries)
            except ImportError:
                pdf = None
                break
            new = [str(n) for n in new]
            if new == pages:
                done = True
                break
            pages = new
        if not done:
            print('! Няма LibreOffice/pypdfium2 или номерата не се установиха - оценени са приблизително')
            pages = estimated_pages(entries)
            build(tmp, entries, pages)
        else:
            main_pages = int(pages[[t for t, _, _ in entries].index(APP_A)]) - 1
            figs, tabs, lsts, refs = update_annotation(main_pages)
            build(tmp, entries, pages)           # анотацията е с постоянна дължина
            pdf = render_pdf(OUT, tmp)
            check, total = find_pages(pdf, entries)
            if [str(n) for n in check] != pages:
                print('! номерата на страниците се промениха след обновяването на анотацията')
            shutil.copy(pdf, PDF)
            print(f'Страници: общо {total}; без приложенията {main_pages}')
            print(f'Анотация: {figs} фигури, {tabs} таблици, {lsts} листинга, {refs} източника')
    print('Готово: {} ({:.1f} MB)'.format(OUT, os.path.getsize(OUT) / 1e6))


if __name__ == '__main__':
    main()

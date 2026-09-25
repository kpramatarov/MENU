#!/usr/bin/env python3
"""Сглобява дипломната работа в един Word документ.
Употреба:  python3 sglobyavane.py      (от папката diplomna_rabota)
Изисква:   pandoc
"""
import re, subprocess, sys, os

PB = '\n```{=openxml}\n<w:p><w:r><w:br w:type="page"/></w:r></w:p>\n```\n\n'
WIDE = ('principna','blokova','uart','obhvat','zakasnenie','master','slave','dht11','releen','izmervane',
        'struktura','opashka','posledovatelnost','telemetria','zashtita')
NARROW = {'maket': '9cm'}          # снимки и високи фигури

TITLE = """::: {custom-style="CenterMed"}
ТЕХНИЧЕСКИ УНИВЕРСИТЕТ – СОФИЯ
:::

::: {custom-style="CenterSml"}
ФАКУЛТЕТ ПО ЕЛЕКТРОННА ТЕХНИКА И ТЕХНОЛОГИИ

Катедра „Електронна техника"
:::

::: {custom-style="CenterBig"}
ДИПЛОМНА РАБОТА
:::

::: {custom-style="CenterSml"}
за придобиване на образователно-квалификационна степен
:::

::: {custom-style="CenterBig"}
„МАГИСТЪР"
:::

::: {custom-style="CenterSml"}
на тема
:::

::: {custom-style="CenterMed"}
СИСТЕМА ЗА УПРАВЛЕНИЕ НА ДОМА ЧРЕЗ IEEE 802.11 (Wi-Fi) ИНТЕРФЕЙС
:::

|  |  |
|---|---|
| **Дипломант:** | инж. Кръстиян Тодоров Праматаров |
| **Факултетен номер:** | 901322003 |
| **Специалност:** | Електронни системи за хибридни и електромобили |
| **Форма на обучение:** | редовна |
| **Научен ръководител:** | доц. д-р инж. Любомир Богданов |
| **Консултант:** | ................................................ |
| **Рецензент:** | ................................................ |

::: {custom-style="CenterMed"}
София, 2023 г.
:::"""

CHAPTERS = [('Glava_1_Uvod_cel_zadachi.md', 'ГЛАВА ПЪРВА. УВОД, ЦЕЛ И ЗАДАЧИ'),
            ('Glava_2_Harduerno_proektirane.md', 'ГЛАВА ВТОРА. ХАРДУЕРНО ПРОЕКТИРАНЕ'),
            ('Glava_3_Softuerno_proektirane.md', 'ГЛАВА ТРЕТА. СОФТУЕРНО ПРОЕКТИРАНЕ'),
            ('Glava_4_Eksperimentalni_rezultati.md', 'ГЛАВА ЧЕТВЪРТА. ЕКСПЕРИМЕНТАЛНИ ИЗСЛЕДВАНИЯ'),
            ('Zaklyuchenie.md', 'ЗАКЛЮЧЕНИЕ')]

def est_pages(t):
    """Ориентировъчен брой страници (A4, Times New Roman 12 pt, 1,5 реда)."""
    pr, tr, hd, fm, im, code = [], 0, 0, 0, 0, 0; inc = False
    for l in t.split('\n'):
        x = l.strip()
        if x.startswith('```'): inc = not inc; continue
        if inc: code += 1; continue
        if x.startswith('|'): tr += 1; continue
        if x.startswith('!['): im += 1; continue
        if x.startswith('#'): hd += 1; continue
        if x.startswith('$$'): fm += 1; continue
        if x in ('', '---'): continue
        pr.append(re.sub(r'[*`>]', '', x))
    return len(' '.join(pr)) / 2100 + tr * .035 + hd * .03 + fm * .045 + im * .38 + code * .022

def regen_toc():
    """Пресъздава Sadarzhanie.md от действителните заглавия – в реда на документа."""
    rows = [('ДЕКЛАРАЦИЯ ЗА ОРИГИНАЛНОСТ', 0, 2), ('АНОТАЦИЯ', 0, 3),
            ('СЪДЪРЖАНИЕ', 0, 5), ('СПИСЪК НА ИЗПОЛЗВАНИТЕ СЪКРАЩЕНИЯ', 0, 7)]
    p = 8.0
    for f, title in CHAPTERS:
        t = open(f, encoding='utf-8').read()
        rows.append((title, 0, round(p)))
        acc = p
        for sec in re.split(r'\n(?=## \d+\.\d+\.)', t)[1:]:
            rows.append((sec.split('\n')[0][3:].strip(), 1, round(acc)))
            acc += est_pages(sec)
        p += est_pages(t)
    rows.append(('ИЗПОЛЗВАНА ЛИТЕРАТУРА', 0, round(p))); p += 1.5
    rows.append(('ПРИЛОЖЕНИЕ А. Графична част – листове 1–3', 0, round(p)))
    out = ['# СЪДЪРЖАНИЕ', '', '| | стр. |', '|---|---|']
    for title, lvl, pg in rows:
        out.append('| {} | {} |'.format(('&nbsp;&nbsp;&nbsp;&nbsp;' + title) if lvl else '**{}**'.format(title), pg))
    out += ['', '> Номерата на страниците са ориентировъчни. При окончателното оформяне в текстов',
            '> редактор съдържанието се генерира автоматично от стиловете на заглавията.', '']
    open('Sadarzhanie.md', 'w', encoding='utf-8').write('\n'.join(out))
    return round(p)

def update_annotation(last_page):
    """Обновява бройките в анотацията от действителното съдържание."""
    allt = '\n'.join(open(f, encoding='utf-8').read() for f, _ in CHAPTERS)
    figs = len(set(re.findall(r'!\[Фиг\. (\d+\.\d+)\]', allt)))      # вградени, не споменати
    tabs = len(re.findall(r'\*\*Таблица \d+\.\d+\.', allt))
    lsts = len(re.findall(r'\*\*Листинг \d+\.\d+\.', allt))
    refs = len(re.findall(r'^\d+\. ', open('Literatura.md', encoding='utf-8').read(), re.M))
    a = open('Anotaciya.md', encoding='utf-8').read()
    a = re.sub(r'Обемът на дипломната работа е \d+ страници и съдържа \d+ фигури, \d+ таблици,\n\d+ листинга и \d+ литературни източника',
               'Обемът на дипломната работа е {} страници и съдържа {} фигури, {} таблици,\n{} листинга и {} литературни източника'
               .format(last_page, figs, tabs, lsts, refs), a)
    open('Anotaciya.md', 'w', encoding='utf-8').write(a)
    return figs, tabs, lsts, refs

def load(p):
    t = open(p, encoding='utf-8').read()
    def img(m):
        alt, src = m.group(1), m.group(2)
        png = src.replace('figuri/', 'figuri/png/').replace('.svg', '.png')
        w = next((v for k, v in NARROW.items() if k in png), None) or \
            ('16cm' if any(k in png for k in WIDE) else '13cm')
        return '![{}]({}){{width={}}}'.format(alt, png, w)
    return re.sub(r'!\[([^\]]*)\]\(([^)]+\.svg)\)', img, t).strip()

def main():
    last = regen_toc() + 2          # + приложението (3 листа, започва от last)
    print('Съдържание: обновено (≈ {} стр.)'.format(last))
    print('Анотация: {} фигури, {} таблици, {} листинга, {} източника'.format(*update_annotation(last)))
    parts = [TITLE, PB]
    decl = load('Zaglavna_chast.md')
    parts += ['# ДЕКЛАРАЦИЯ ЗА ОРИГИНАЛНОСТ' + decl.split('# ДЕКЛАРАЦИЯ ЗА ОРИГИНАЛНОСТ')[1].strip()]
    for p in ('Anotaciya.md', 'Sadarzhanie.md', 'Sakrashteniya.md'):
        parts += [PB, load(p)]
    for p in ('Glava_1_Uvod_cel_zadachi.md', 'Glava_2_Harduerno_proektirane.md',
              'Glava_3_Softuerno_proektirane.md', 'Glava_4_Eksperimentalni_rezultati.md',
              'Zaklyuchenie.md', 'Literatura.md'):
        parts += [PB, load(p)]
    app = ['# ПРИЛОЖЕНИЕ А', '', '## Графична част – листове 1…3', '',
           'Листовете са изпълнени във формат A1 (841 × 594 mm) с основен надпис по ЕСКД.',
           'Тук са дадени в умален вид; оригиналите са приложени отделно.', '']
    sheets = [('List_1_Blokova_shema', 'Лист 1. Блокова схема на хардуера (т. 5.1)'),
              ('List_2_Principna_shema', 'Лист 2. Принципна електрическа схема (т. 5.2)'),
              ('List_3_Algoritam', 'Лист 3. Алгоритъм на управляващата програма (т. 5.3)')]
    for n, (f, cap) in enumerate(sheets):
        app += ['![{}](grafichna_chast/{}.png){{width=16cm}}'.format(cap, f), '',
                '**{}**'.format(cap), '']
        if n < 2: app.append(PB)
    parts += [PB, '\n'.join(app)]

    md = '\n\n'.join(parts)
    open('_sglobyavane_izhoden.md', 'w', encoding='utf-8').write(md)
    out = 'Diplomna_rabota_Pramatarov.docx'
    subprocess.run(['pandoc', '_sglobyavane_izhoden.md',
                    '--reference-doc=_sglobyavane_stilove.docx', '--resource-path=.',
                    '-f', 'markdown+tex_math_dollars+pipe_tables+fenced_divs+raw_attribute',
                    '-t', 'docx', '-o', out], check=True)
    print('Готово: {} ({:.1f} MB)'.format(out, os.path.getsize(out) / 1e6))

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Сглобява дипломната работа в един Word документ.
Употреба:  python3 sglobyavane.py      (от папката diplomna_rabota)
Изисква:   pandoc
"""
import re, subprocess, sys, os

PB = '\n```{=openxml}\n<w:p><w:r><w:br w:type="page"/></w:r></w:p>\n```\n\n'
WIDE = ('principna','blokova','uart','obhvat','zakasnenie','master','slave')

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

def load(p):
    t = open(p, encoding='utf-8').read()
    def img(m):
        alt, src = m.group(1), m.group(2)
        png = src.replace('figuri/', 'figuri/png/').replace('.svg', '.png')
        w = '16cm' if any(k in png for k in WIDE) else '13cm'
        return '![{}]({}){{width={}}}'.format(alt, png, w)
    return re.sub(r'!\[([^\]]*)\]\(([^)]+\.svg)\)', img, t).strip()

def main():
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
    sheets = [('List_1_Blokova_shema', 'Лист 1. Блокова схема на системата (т. 5.1)'),
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

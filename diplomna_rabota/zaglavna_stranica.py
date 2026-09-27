#!/usr/bin/env python3
"""Заглавна страница по образеца на ТУ – София (вариант 1, на български и на английски).

1. title_pages() – двете страници като OpenXML за сглобяването на дипломната работа
   (отделен раздел без номер на страницата: логото на ТУ, водният знак и текстът на образеца).
2. python3 zaglavna_stranica.py – попълва самия образец zaglavna/obrazec_var-1_BGENG.docx и
   записва Zaglavna_stranica_Pramatarov.docx (за разпечатване и подпис).

Образецът е на Стопанския факултет. Работата е във Факултета по електронна техника и
технологии, затова името на факултета е сменено, а логото на Стопанския факултет е махнато.
Ако се сложи файл zaglavna/logo_fett.png (логото на ФЕТТ в добра резолюция), то се поставя
вдясно в заглавието на страницата, както е в образеца.
"""
import os
import re
import shutil
import zipfile

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zaglavna')
OBRAZEC = os.path.join(DIR, 'obrazec_var-1_BGENG.docx')
POPALNEN = 'Zaglavna_stranica_Pramatarov.docx'

BLUE = '17365D'                       # цветът на надписа в образеца
EMU_PT = 12700

BG = {'univ': 'ТЕХНИЧЕСКИ УНИВЕРСИТЕТ-СОФИЯ',
      'fac': ['ФАКУЛТЕТ ПО ЕЛЕКТРОННА ТЕХНИКА', 'И ТЕХНОЛОГИИ'],
      'title': 'ДИПЛОМНА РАБОТА',
      'degree': 'за придобиване на ОКС „Магистър“ по „Електронни системи за хибридни и '
                'електромобили“ от ПН: 5.2 Електротехника, електроника и автоматика',
      'topic_label': 'Тема: ',
      'topic': '„Система за управление на дома чрез IEEE 802.11 (Wi-Fi) интерфейс“',
      'people': [('Изготвил', 'инж. Кръстиян Тодоров Праматаров', True),
                 ('Факултетен №', '901322003', True),
                 None,
                 ('Научен ръководител', 'доц. д-р инж. Любомир Богданов', False)]}
EN = {'univ': 'TECHNICAL UNIVERSITY OF SOFIA',
      'fac': ['FACULTY OF ELECTRONIC ENGINEERING', 'AND TECHNOLOGIES'],
      'title': 'MASTER’S THESIS',
      'degree': '“Electronic Systems for Hybrid and Electric Vehicles”, Professional Field: '
                '5.2 Electrical Engineering, Electronics and Automation',
      'topic_label': 'Topic: ',
      'topic': '“Home Control System via IEEE 802.11 (Wi-Fi) Interface”',
      'people': [('Submitted by', 'Krastiyan Todorov Pramatarov', True),
                 ('Faculty №', '901322003', True),
                 None,
                 ('Supervisor', 'Assoc. Prof. Lyubomir Bogdanov, PhD', False)]}

# изображенията: rId в документа, файл, име в архива
MEDIA = {'rIdTpTu': ('logo_tu.jpeg', 'tp_logo_tu.jpeg'),
         'rIdTpWm': ('voden_znak.jpeg', 'tp_voden_znak.jpeg'),
         'rIdTpFett': ('logo_fett.png', 'tp_logo_fett.png')}


def esc(t):
    return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def run(text, pt, bold=False, italic=False, font='Times New Roman', color=None, spacing=None):
    rpr = f'<w:rFonts w:ascii="{font}" w:hAnsi="{font}" w:cs="{font}"/>'
    rpr += '<w:b/><w:bCs/>' if bold else ''
    rpr += '<w:i/><w:iCs/>' if italic else ''
    rpr += f'<w:color w:val="{color}"/>' if color else ''
    rpr += f'<w:spacing w:val="{spacing}"/>' if spacing else ''
    rpr += f'<w:sz w:val="{round(pt * 2)}"/><w:szCs w:val="{round(pt * 2)}"/>'
    return f'<w:r><w:rPr>{rpr}</w:rPr><w:t xml:space="preserve">{esc(text)}</w:t></w:r>'


def par(content, jc='center', before=0, after=200, line=276, first='', border='', sect='', rule='auto'):
    """Абзац с пряко зададено оформление (без стил), както в образеца: след абзаца 10 pt,
    междуредие 1,15. first - pageBreakBefore; border - долна линия; sect - край на раздела."""
    ppr = (f'{first}{border}<w:spacing w:before="{before}" w:after="{after}" w:line="{line}" '
           f'w:lineRule="{rule}"/><w:ind w:left="0" w:right="0" w:firstLine="0"/><w:jc w:val="{jc}"/>{sect}')
    return f'<w:p><w:pPr>{ppr}</w:pPr>{content}</w:p>'


def pic(rid, cx, cy, pid, name):
    return ('<a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
            f'<pic:pic><pic:nvPicPr><pic:cNvPr id="{pid}" name="{name}"/><pic:cNvPicPr/></pic:nvPicPr>'
            f'<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
            f'<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
            '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic>')


def inline(rid, cx, cy, pid, name):
    return (f'<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="{cx}" '
            f'cy="{cy}"/><wp:effectExtent l="0" t="0" r="0" b="0"/><wp:docPr id="{pid}" name="{name}"/>'
            '<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect="1"/></wp:cNvGraphicFramePr>'
            f'{pic(rid, cx, cy, pid, name)}</wp:inline></w:drawing></w:r>')


def watermark(pid):
    """Водният знак на образеца - логото на ТУ зад текста, в средата на полето за текст."""
    cx, cy = round(453.6 * EMU_PT), round(483.8 * EMU_PT)
    return ('<w:r><w:drawing><wp:anchor distT="0" distB="0" distL="0" distR="0" simplePos="0" '
            'relativeHeight="0" behindDoc="1" locked="1" layoutInCell="0" allowOverlap="1">'
            '<wp:simplePos x="0" y="0"/><wp:positionH relativeFrom="margin"><wp:align>center</wp:align>'
            '</wp:positionH><wp:positionV relativeFrom="margin"><wp:align>center</wp:align></wp:positionV>'
            f'<wp:extent cx="{cx}" cy="{cy}"/><wp:effectExtent l="0" t="0" r="0" b="0"/><wp:wrapNone/>'
            f'<wp:docPr id="{pid}" name="Воден знак"/><wp:cNvGraphicFramePr/>'
            f'{pic("rIdTpWm", cx, cy, pid, "Воден знак")}</wp:anchor></w:drawing></w:r>')


def header_table(d, pid):
    """Заглавието на страницата: логото на ТУ, името на университета, линия, факултетът."""
    w_logo, w_text = 1474, 9072 - 2 * 1474
    nil = ''.join(f'<w:{s} w:val="nil"/>' for s in ('top', 'left', 'bottom', 'right', 'insideH', 'insideV'))

    def cell(w, body):
        return (f'<w:tc><w:tcPr><w:tcW w:w="{w}" w:type="dxa"/><w:vAlign w:val="center"/></w:tcPr>'
                f'{body}</w:tc>')
    logo = par(inline('rIdTpTu', 834888, 890704, pid, 'Лого на ТУ – София'), after=0, line=240)
    fett = os.path.exists(os.path.join(DIR, MEDIA['rIdTpFett'][0]))
    right = par(inline('rIdTpFett', 799465, 603850, pid + 1, 'Лого на факултета') if fett else '',
                after=0, line=240)
    rule = f'<w:pBdr><w:bottom w:val="single" w:sz="8" w:space="2" w:color="{BLUE}"/></w:pBdr>'
    text = par(run(d['univ'], 18, font='Calibri', color=BLUE), after=40, line=240, border=rule)
    text += ''.join(par(run(t, 14, bold=True, font='Calibri', color=BLUE), after=0, line=240)
                    for t in d['fac'])
    return ('<w:tbl><w:tblPr><w:tblW w:w="9072" w:type="dxa"/><w:jc w:val="center"/>'
            f'<w:tblBorders>{nil}</w:tblBorders><w:tblLayout w:type="fixed"/><w:tblCellMar>'
            '<w:left w:w="0" w:type="dxa"/><w:right w:w="0" w:type="dxa"/></w:tblCellMar>'
            '<w:tblLook w:val="0000"/></w:tblPr><w:tblGrid>'
            f'<w:gridCol w:w="{w_logo}"/><w:gridCol w:w="{w_text}"/><w:gridCol w:w="{w_logo}"/></w:tblGrid>'
            f'<w:tr>{cell(w_logo, logo)}{cell(w_text, text)}{cell(w_logo, right)}</w:tr></w:tbl>')


SECT = ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1417" w:right="1417" '
        'w:bottom="1417" w:left="1417" w:header="708" w:footer="708" w:gutter="0"/>'
        '<w:pgNumType w:start="1"/><w:cols w:space="708"/><w:docGrid w:linePitch="360"/></w:sectPr>')


def page(d, pid, first_page):
    """Една страница по образеца; разстоянията по вертикала следват образеца."""
    brk = '' if first_page else '<w:pageBreakBefore/>'
    x = par(watermark(pid), after=0, line=20, first=brk, rule='exact')     # 1 pt - само за водния знак
    x += header_table(d, pid + 1)
    x += par(run(d['title'], 36, bold=True, spacing=18), before=2600, after=120)
    x += par(run(d['degree'], 18, bold=True, spacing=10), after=200)
    x += par(run(d['topic_label'], 18, bold=True) + run(d['topic'], 18, bold=True, italic=True),
             before=560, after=200)
    people = ''
    before = 2500
    for item in d['people']:
        if item is None:
            before = 360
            continue
        label, value, italic = item
        people += par(run(label, 14, bold=True) + run(': ', 14) + run(value, 14, italic=italic),
                      jc='left', before=before, after=200)
        before = 0
    return x + people


def title_pages():
    """Българската и английската заглавна страница като OpenXML; последният абзац затваря
    раздела (без колонтитул, т.е. без номер на страницата)."""
    x = page(BG, 9101, True) + page(EN, 9111, False)
    return x[:x.rindex('</w:pPr>')] + SECT + x[x.rindex('</w:pPr>'):]


def add_media(files):
    """Добавя изображенията и връзките към тях в архива на документа (извиква се от
    sglobyavane.py след pandoc)."""
    rels = files['word/_rels/document.xml.rels'].decode('utf-8')
    ct = files['[Content_Types].xml'].decode('utf-8')
    for rid, (src, name) in MEDIA.items():
        path = os.path.join(DIR, src)
        if not os.path.exists(path):
            continue
        files['word/media/' + name] = open(path, 'rb').read()
        rels = rels.replace('</Relationships>', f'<Relationship Id="{rid}" Type="http://schemas.'
                            'openxmlformats.org/officeDocument/2006/relationships/image" '
                            f'Target="media/{name}"/></Relationships>')
        mime = 'image/png' if name.endswith('.png') else 'image/jpeg'
        ct = ct.replace('</Types>', f'<Override PartName="/word/media/{name}" ContentType="{mime}"/></Types>')
    files['word/_rels/document.xml.rels'] = rels.encode('utf-8')
    files['[Content_Types].xml'] = ct.encode('utf-8')


# ------------------------------------------------ попълване на самия образец --
def top_paragraphs(doc):
    """(начало, край) на абзаците от първо ниво - абзаците в текстовите полета са вложени."""
    out, depth, start = [], 0, 0
    for m in re.finditer(r'<w:p(?=[ >/])[^>]*?(/?)>|</w:p>', doc):
        if m.group(0) == '</w:p>':
            depth -= 1
            if depth == 0:
                out.append((start, m.end()))
        elif m.group(1) == '/':
            if depth == 0:
                out.append((m.start(), m.end()))
        else:
            if depth == 0:
                start = m.start()
            depth += 1
    return out


def tnr(parts, pt, lang=None, spacing=None):
    """Пасажи в Times New Roman; parts - (текст, удебелен, курсив)."""
    x = ''
    for text, bold, italic in parts:
        rpr = '<w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" w:cs="Times New Roman"/>'
        rpr += ('<w:b/>' if bold else '') + ('<w:i/>' if italic else '')
        rpr += f'<w:spacing w:val="{spacing}"/>' if spacing else ''
        rpr += f'<w:sz w:val="{pt * 2}"/><w:szCs w:val="{pt * 2}"/>'
        rpr += f'<w:lang w:val="{lang}"/>' if lang else ''
        x += f'<w:r><w:rPr>{rpr}</w:rPr><w:t xml:space="preserve">{esc(text)}</w:t></w:r>'
    return x


def popalni_obrazec(out=POPALNEN):
    """Попълва образеца: данните на дипломанта, ФЕТТ вместо Стопанския факултет, без логото
    на Стопанския факултет. Оформлението на абзаците (pPr) се запазва."""
    z = zipfile.ZipFile(OBRAZEC)
    files = {n: z.read(n) for n in z.namelist()}
    z.close()
    doc = re.sub(r' w:rsid\w*="[^"]*"', '', files['word/document.xml'].decode('utf-8'))

    # логото на Стопанския факултет (rId6) - и на двете страници
    doc, n = re.subn(r'<w:r><w:rPr><w:noProof/><w:lang w:eastAsia="bg-BG"/></w:rPr><w:drawing>'
                     r'(?:(?!</w:drawing>).)*?r:embed="rId6"(?:(?!</w:drawing>).)*?</w:drawing></w:r>',
                     '', doc, flags=re.S)
    assert n == 2, n
    # името на факултета - на два реда по 14 pt; текстовото поле става по-високо
    for old_name, lines in (('СТОПАНСКИ ФАКУЛТЕТ', BG['fac']), ('FACULTY OF MANAGEMENT', EN['fac'])):
        i = doc.index(f'<w:t>{old_name}</w:t>')
        box = doc.rindex('<v:shape ', 0, i)
        doc = doc[:box] + doc[box:i].replace('height:32.4pt', 'height:46pt', 1) + doc[i:]
        i = doc.index(f'<w:t>{old_name}</w:t>')
        a, b = doc.rindex('<w:p>', 0, i), doc.index('</w:p>', i) + len('</w:p>')
        p = doc[a:b].replace('<w:sz w:val="36"/><w:szCs w:val="36"/>', '<w:sz w:val="28"/><w:szCs w:val="28"/>')
        p = p.replace(f'<w:t>{old_name}</w:t>', f'<w:t>{esc(lines[0])}</w:t><w:br/><w:t>{esc(lines[1])}</w:t>')
        doc = doc[:a] + p + doc[b:]

    bg_lab = [(BG['people'][0][0], BG['people'][0][1]), (BG['people'][1][0], BG['people'][1][1])]
    en_lab = [(EN['people'][0][0], EN['people'][0][1]), (EN['people'][1][0], EN['people'][1][1])]
    targets = [  # (начало на текста в образеца, нови пасажи, размер, език, разредка)
        ('за придобиване на ОКС', [(BG['degree'], True, False)], 18, None, 10),
        ('Тема:', [(BG['topic_label'], True, False), (BG['topic'], True, True)], 18, None, None),
        ('Изготвил(а):', [(bg_lab[0][0], True, False), (': ', False, False), (bg_lab[0][1], False, True)],
         14, None, None),
        ('Факултетен №:', [(bg_lab[1][0], True, False), (': ', False, False), (bg_lab[1][1], False, True)],
         14, None, None),
        ('Научен ръководител:', [(BG['people'][3][0], True, False), (': ', False, False),
                                 (BG['people'][3][1], False, False)], 14, None, None),
        ('BACHALOR’S', [(EN['title'], True, False)], 36, 'en-US', 18),
        ('“Specialty”', [(EN['degree'], True, False)], 18, 'en-US', 10),
        ('Topic:', [(EN['topic_label'], True, False), (EN['topic'], True, True)], 18, 'en-US', None),
        ('Submitted by:', [(en_lab[0][0], True, False), (': ', False, False), (en_lab[0][1], False, True)],
         14, 'en-US', None),
        ('Faculty №:', [(en_lab[1][0], True, False), (': ', False, False), (en_lab[1][1], False, True)],
         14, 'en-US', None),
        ('Supervisor:', [(EN['people'][3][0], True, False), (': ', False, False),
                         (EN['people'][3][1], False, False)], 14, 'en-US', None)]
    done = set()
    for a, b in reversed(top_paragraphs(doc)):
        seg = doc[a:b]
        if '<w:txbxContent>' in seg:
            continue
        text = ''.join(re.findall(r'<w:t(?: [^>]*)?>([^<]*)</w:t>', seg)).strip()
        for key, parts, pt, lang, sp in targets:
            if text.startswith(key):
                m = re.match(r'<w:p>(<w:pPr>.*?</w:pPr>)?', seg, re.S)
                doc = doc[:a] + m.group(0) + tnr(parts, pt, lang, sp) + '</w:p>' + doc[b:]
                done.add(key)
                break
    missing = [k for k, *_ in targets if k not in done]
    assert not missing, missing
    assert '…' not in doc and 'Иван' not in doc and 'Ivan' not in doc, 'остава непопълнено поле'
    files['word/document.xml'] = doc.encode('utf-8')
    files.pop('word/media/image1.jpeg', None)
    rels = files['word/_rels/document.xml.rels'].decode('utf-8')
    files['word/_rels/document.xml.rels'] = re.sub(r'<Relationship Id="rId6" [^>]*/>', '', rels).encode('utf-8')
    tmp = out + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zo:
        for n, data in files.items():
            zo.writestr(n, data)
    shutil.move(tmp, out)
    return out


if __name__ == '__main__':
    print('Готово:', popalni_obrazec())

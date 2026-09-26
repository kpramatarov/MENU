#!/usr/bin/env python3
"""Създава шаблона със стиловете на Word документа (_sglobyavane_stilove.docx).

Употреба:  python3 stilove_word.py      (от папката diplomna_rabota)
Изисква:   pandoc

Оформлението следва методическите указания на катедра „Електронна техника“:
шрифт 14 pt при 60–66 знака на ред и 30–34 реда на страница.
  - A4, полета: ляво 30 mm (за подвързване), дясно 25 mm, горно и долно 25 mm -> ширина на
    текста 155 mm; при Times New Roman 14 pt и двустранно подравняване това дава средно около
    63 знака на ред (проверено с изобразяване на документа в PDF);
  - междуредие 1,4 -> 32 реда на страница;
  - номер на страницата долу в средата; заглавната страница е без номер.
Шаблонът се строи наново от стандартния шаблон на pandoc, за да бъде възпроизводим.
"""
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

OUT = Path('_sglobyavane_stilove.docx')
TNR = ('<w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" '
       'w:cs="Times New Roman" w:eastAsia="Times New Roman"/>')
MONO = '<w:rFonts w:ascii="Courier New" w:hAnsi="Courier New" w:cs="Courier New"/>'
TEXT_W = 8787                      # ширина на текста в twips (155 mm)
LINE = 336                         # междуредие 1,4 (240 = единично)


def sz(pt):
    return f'<w:sz w:val="{int(pt * 2)}"/><w:szCs w:val="{int(pt * 2)}"/>'


def pstyle(sid, name, ppr='', rpr='', based='Normal', nxt=None, extra=''):
    nx = f'<w:next w:val="{nxt}"/>' if nxt else ''
    return (f'<w:style w:type="paragraph" w:customStyle="1" w:styleId="{sid}"><w:name w:val="{name}"/>'
            f'<w:basedOn w:val="{based}"/>{nx}<w:qFormat/>{extra}<w:pPr>{ppr}</w:pPr><w:rPr>{rpr}</w:rPr></w:style>')


def heading(level, size, jc, before, after, bold=True, italic=False):
    rpr = TNR + ('<w:b/><w:bCs/>' if bold else '') + ('<w:i/><w:iCs/>' if italic else '') + sz(size)
    ppr = (f'<w:keepNext/><w:keepLines/><w:spacing w:before="{before}" w:after="{after}" w:line="276" '
           f'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="{jc}"/><w:outlineLvl w:val="{level - 1}"/>')
    return pstyle(f'Heading{level}', f'heading {level}', ppr, rpr, nxt='BodyText')


STYLES = {
    'Normal': ('<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/>'
               '<w:pPr><w:widowControl/></w:pPr><w:rPr/></w:style>'),
    'BodyText': pstyle('BodyText', 'Body Text',
                       '<w:spacing w:before="0" w:after="0"/><w:ind w:firstLine="567"/><w:jc w:val="both"/>'),
    'FirstParagraph': pstyle('FirstParagraph', 'First Paragraph', '', '', based='BodyText', nxt='BodyText'),
    # стегнатите списъци в текста са като основния текст, но без отстъп на първия ред
    'Compact': pstyle('Compact', 'Compact', '<w:spacing w:before="0" w:after="0"/><w:ind w:firstLine="0"/>',
                      '', based='BodyText'),
    'TableText': pstyle('TableText', 'Table Text',
                        '<w:spacing w:before="20" w:after="20" w:line="240" w:lineRule="auto"/>'
                        '<w:ind w:firstLine="0"/><w:jc w:val="left"/>', sz(12)),
    'Heading1': heading(1, 16, 'center', 0, 240),
    'Heading2': heading(2, 14, 'left', 360, 120),
    'Heading3': heading(3, 14, 'left', 240, 120, italic=True),
    'Heading4': heading(4, 14, 'left', 200, 60, bold=False, italic=True),
    'Figure': pstyle('Figure', 'Figure', '<w:keepNext/><w:spacing w:before="120" w:after="0" w:line="240" '
                     'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="center"/>'),
    'CaptionedFigure': pstyle('CaptionedFigure', 'Captioned Figure', '', '', based='Figure'),
    'ImageCaption': pstyle('ImageCaption', 'Image Caption', '<w:spacing w:before="60" w:after="240" w:line="240" '
                           'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="center"/>', '<w:b/>' + sz(12)),
    'TableCaption': pstyle('TableCaption', 'Table Caption', '<w:keepNext/><w:spacing w:before="120" w:after="60"/>'
                           '<w:ind w:firstLine="0"/><w:jc w:val="left"/>', '<w:b/>'),
    'SourceCode': pstyle('SourceCode', 'Source Code', '<w:spacing w:before="0" w:after="0" w:line="240" '
                         'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="left"/>'
                         '<w:shd w:val="clear" w:color="auto" w:fill="F4F4F4"/>', MONO + sz(8.5)),
    'BlockText': pstyle('BlockText', 'Block Text', '<w:spacing w:before="60" w:after="60" w:line="276" '
                        'w:lineRule="auto"/><w:ind w:left="567" w:firstLine="0"/>', '<w:i/>' + sz(12)),
    'Centered': pstyle('Centered', 'Centered', '<w:spacing w:before="0" w:after="60"/><w:ind w:firstLine="0"/>'
                       '<w:jc w:val="center"/>'),
    'CenterBig': pstyle('CenterBig', 'CenterBig', '<w:spacing w:before="240" w:after="240" w:line="276" '
                        'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="center"/>', '<w:b/>' + sz(18)),
    'CenterMed': pstyle('CenterMed', 'CenterMed', '<w:spacing w:before="160" w:after="160" w:line="276" '
                        'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="center"/>', '<w:b/>' + sz(16)),
    'CenterSml': pstyle('CenterSml', 'CenterSml', '<w:spacing w:before="60" w:after="60" w:line="276" '
                        'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="center"/>', sz(14)),
    # ръчно съдържание: точки до номера на страницата
    'TOC1': pstyle('TOC1', 'toc 1', f'<w:tabs><w:tab w:val="right" w:leader="dot" w:pos="{TEXT_W}"/></w:tabs>'
                   '<w:spacing w:before="60" w:after="0" w:line="252" w:lineRule="auto"/><w:ind w:right="567" '
                   'w:firstLine="0"/><w:jc w:val="left"/>', '<w:b/>'),
    'TOC2': pstyle('TOC2', 'toc 2', f'<w:tabs><w:tab w:val="right" w:leader="dot" w:pos="{TEXT_W}"/></w:tabs>'
                   '<w:spacing w:before="0" w:after="0" w:line="252" w:lineRule="auto"/><w:ind w:left="397" '
                   'w:right="567" w:hanging="0"/><w:jc w:val="left"/>'),
    'Footer': pstyle('Footer', 'footer', '<w:ind w:firstLine="0"/><w:jc w:val="center"/>', sz(12)),
    # заглавна страница: данни с табулация и редове за подпис
    'TitleInfo': pstyle('TitleInfo', 'Title Info', '<w:tabs><w:tab w:val="left" w:pos="3119"/></w:tabs>'
                        '<w:spacing w:before="0" w:after="60" w:line="276" w:lineRule="auto"/>'
                        '<w:ind w:left="3119" w:hanging="3119"/><w:jc w:val="left"/>'),
    'Signature': pstyle('Signature', 'Signature', '<w:spacing w:before="360" w:after="0" w:line="276" '
                        'w:lineRule="auto"/><w:ind w:firstLine="0"/><w:jc w:val="right"/>'),
}

TABLE = ('<w:style w:type="table" w:default="1" w:styleId="Table"><w:name w:val="Table"/>'
         '<w:basedOn w:val="TableNormal"/><w:qFormat/><w:pPr><w:spacing w:line="240" w:lineRule="auto"/>'
         '<w:ind w:firstLine="0"/></w:pPr><w:rPr>' + sz(12) + '</w:rPr><w:tblPr><w:tblInd w:w="0" w:type="dxa"/>'
         '<w:tblBorders>' + ''.join(f'<w:{b} w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
                                    for b in ('top', 'left', 'bottom', 'right', 'insideH', 'insideV')) +
         '</w:tblBorders><w:tblCellMar><w:top w:w="0" w:type="dxa"/><w:left w:w="85" w:type="dxa"/>'
         '<w:bottom w:w="0" w:type="dxa"/><w:right w:w="85" w:type="dxa"/></w:tblCellMar></w:tblPr>'
         '<w:tblStylePr w:type="firstRow"><w:rPr><w:b/><w:bCs/></w:rPr><w:tcPr>'
         '<w:shd w:val="clear" w:color="auto" w:fill="EFEFEF"/></w:tcPr></w:tblStylePr></w:style>')

DEFAULTS = ('<w:docDefaults><w:rPrDefault><w:rPr>' + TNR + sz(14) +
            '<w:lang w:val="bg-BG" w:eastAsia="en-US" w:bidi="ar-SA"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr>'
            f'<w:spacing w:after="0" w:line="{LINE}" w:lineRule="auto"/><w:jc w:val="both"/></w:pPr>'
            '</w:pPrDefault></w:docDefaults>')

FOOTER = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
          '<w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
          'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
          '<w:p><w:pPr><w:pStyle w:val="Footer"/><w:jc w:val="center"/></w:pPr>'
          '<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r>'
          '<w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:t>2</w:t></w:r>'
          '<w:r><w:fldChar w:fldCharType="end"/></w:r></w:p></w:ftr>')

SECT = ('<w:sectPr><w:footerReference w:type="default" r:id="rIdFooter1"/>'
        '<w:pgSz w:w="11906" w:h="16838"/>'
        '<w:pgMar w:top="1418" w:right="1418" w:bottom="1418" w:left="1701" w:header="709" w:footer="709" '
        'w:gutter="0"/><w:pgNumType w:start="1"/><w:titlePg/><w:docGrid w:linePitch="360"/></w:sectPr>')


def main():
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp) / 'default.docx'
        base.write_bytes(subprocess.run(['pandoc', '--print-default-data-file', 'reference.docx'],
                                        check=True, capture_output=True).stdout)
        src = zipfile.ZipFile(base)
        files = {n: src.read(n) for n in src.namelist()}

    st = files['word/styles.xml'].decode('utf-8')
    st = re.sub(r'<w:docDefaults>.*?</w:docDefaults>', DEFAULTS, st, flags=re.S)
    for sid, xml in list(STYLES.items()) + [('Table', TABLE)]:
        pat = r'<w:style [^>]*w:styleId="%s"[^>]*>.*?</w:style>' % sid
        if re.search(pat, st, flags=re.S):
            st = re.sub(pat, lambda m: xml, st, count=1, flags=re.S)
        else:
            st = st.replace('</w:styles>', xml + '</w:styles>')
    # вграденият стил за код в текста - по-малък от основния текст
    st = re.sub(r'(<w:style [^>]*w:styleId="VerbatimChar"[^>]*>.*?<w:rPr>).*?(</w:rPr>)',
                lambda m: m.group(1) + MONO + sz(12) + m.group(2), st, count=1, flags=re.S)
    files['word/styles.xml'] = st.encode('utf-8')

    doc = files['word/document.xml'].decode('utf-8')
    doc = re.sub(r'<w:body>.*</w:body>', '<w:body><w:p/>' + SECT + '</w:body>', doc, flags=re.S)
    files['word/document.xml'] = doc.encode('utf-8')
    files['word/footer1.xml'] = FOOTER.encode('utf-8')

    rels = files['word/_rels/document.xml.rels'].decode('utf-8')
    rels = rels.replace('</Relationships>', '<Relationship Id="rIdFooter1" Type="http://schemas.openxmlformats.org/'
                        'officeDocument/2006/relationships/footer" Target="footer1.xml"/></Relationships>')
    files['word/_rels/document.xml.rels'] = rels.encode('utf-8')
    ct = files['[Content_Types].xml'].decode('utf-8')
    ct = ct.replace('</Types>', '<Override PartName="/word/footer1.xml" ContentType="application/'
                    'vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/></Types>')
    files['[Content_Types].xml'] = ct.encode('utf-8')

    tmp_out = OUT.with_suffix('.tmp')
    with zipfile.ZipFile(tmp_out, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, data in files.items():
            z.writestr(n, data)
    shutil.move(tmp_out, OUT)
    print('Готово:', OUT)


if __name__ == '__main__':
    main()

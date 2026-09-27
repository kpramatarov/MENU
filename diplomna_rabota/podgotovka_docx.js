#!/usr/bin/env node
// Word документът „Подготовка за защитата“ от Podgotovka_za_zashtitata.md.
//
// Употреба (от папката diplomna_rabota):  node podgotovka_docx.js
// Изисква: Node.js и пакета docx (npm install docx).
//
// Разпознава подмножеството Markdown, използвано в документа: заглавия #, ##, ###,
// абзаци, списъци „- “ и „1. “, удебелен текст и `код`. Освен това:
//   „**Какво да кажа:** …“  – рамка с текста за изговаряне;
//   „**На екрана:** …“      – сив ред с това, което е на слайда;
//   „**N. …?**“             – въпрос в т. 3 (стои на една страница с отговора).
'use strict';
const fs = require('fs');
const path = require('path');
const {
  AlignmentType, BorderStyle, Document, Footer, HeadingLevel, LevelFormat, Packer, PageNumber,
  Paragraph, ShadingType, Table, TableCell, TableRow, TextRun, WidthType,
} = require('docx');

const SRC = path.join(__dirname, 'Podgotovka_za_zashtitata.md');
const OUT = path.join(__dirname, 'Podgotovka_za_zashtitata.docx');

const FONT = 'Arial';
const MONO = 'Courier New';
const NAVY = '17365D';
const INK = '16202E';
const GREY = '5B6778';
const ACCENT = 'B85A12';
const BOX = 'EEF2F7';
const RULE = 'C9D3DF';
const PAGE_W = 11906;               // A4 в DXA (1440 = 1″)
const PAGE_H = 16838;
const MARGIN = 1134;                // 2 cm
const CONTENT_W = PAGE_W - 2 * MARGIN;

// ------------------------------------------------------------------ текст в абзаца
function runs(text, base = {}) {
  const out = [];
  const re = /(\*\*[^*]+\*\*|`[^`]+`)/g;
  let last = 0;
  let m;
  while ((m = re.exec(text))) {
    if (m.index > last) out.push(new TextRun({ ...base, text: text.slice(last, m.index) }));
    const t = m[0];
    if (t.startsWith('**')) out.push(new TextRun({ ...base, text: t.slice(2, -2), bold: true }));
    else out.push(new TextRun({ ...base, text: t.slice(1, -1), font: MONO }));
    last = m.index + t.length;
  }
  if (last < text.length) out.push(new TextRun({ ...base, text: text.slice(last) }));
  return out;
}

// ------------------------------------------------------------------ блокове
const LIST = /^(- |\d+\. )(.*)$/;
const HEAD = /^(#{1,3}) (.*)$/;

function parse(md) {
  const lines = md.split('\n');
  const blocks = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    let m;
    if (!line.trim()) {
      i++;
    } else if ((m = HEAD.exec(line))) {
      blocks.push({ type: 'h', level: m[1].length, text: m[2] });
      i++;
    } else if (LIST.test(line)) {
      const ordered = !line.startsWith('- ');
      const items = [];
      while (i < lines.length && (m = LIST.exec(lines[i]))) {
        let text = m[2];
        i++;
        while (i < lines.length && /^\s{2,}\S/.test(lines[i])) text += ' ' + lines[i++].trim();
        items.push(text);
      }
      blocks.push({ type: 'list', ordered, items });
    } else {
      let text = line.trim();
      i++;
      while (i < lines.length && lines[i].trim() && !HEAD.test(lines[i]) && !LIST.test(lines[i])) {
        text += ' ' + lines[i++].trim();
      }
      blocks.push({ type: 'p', text });
    }
  }
  return blocks;
}

// ------------------------------------------------------------------ елементи
const rule = { style: BorderStyle.SINGLE, size: 4, color: RULE };

function speechBox(text) {
  return new Table({
    width: { size: CONTENT_W, type: WidthType.DXA },
    columnWidths: [CONTENT_W],
    rows: [new TableRow({
      cantSplit: true,
      children: [new TableCell({
        width: { size: CONTENT_W, type: WidthType.DXA },
        shading: { type: ShadingType.CLEAR, color: 'auto', fill: BOX },
        margins: { top: 140, bottom: 160, left: 220, right: 220 },
        borders: { top: rule, bottom: rule, left: rule, right: rule },
        children: [
          new Paragraph({
            spacing: { after: 60 },
            children: [new TextRun({ text: 'КАКВО ДА КАЖА', bold: true, size: 18, color: ACCENT, characterSpacing: 20 })],
          }),
          new Paragraph({ spacing: { after: 0, line: 312 }, children: runs(text, { size: 26, color: INK }) }),
        ],
      })],
    })],
  });
}

function onScreen(text) {
  return new Paragraph({
    keepNext: true,
    spacing: { after: 100 },
    children: [new TextRun({ text: 'На екрана: ', bold: true, size: 21, color: GREY }), ...runs(text, { size: 21, color: GREY })],
  });
}

function slideHeading(text) {
  const [name, time] = text.split(' · ');
  const children = runs(name);
  if (time) children.push(new TextRun({ text: `   ${time}`, bold: false, size: 22, color: GREY }));
  return new Paragraph({ heading: HeadingLevel.HEADING_2, children });
}

// ------------------------------------------------------------------ документ
function build(blocks) {
  const children = [];
  let instance = 0;
  let afterBox = false;
  for (const b of blocks) {
    const before = afterBox ? 200 : undefined;
    afterBox = false;
    let m;
    if (b.type === 'h' && b.level === 1) {
      children.push(new Paragraph({ heading: HeadingLevel.TITLE, children: runs(b.text) }));
    } else if (b.type === 'h' && b.level === 2) {
      children.push(new Paragraph({
        heading: HeadingLevel.HEADING_1,
        pageBreakBefore: /^[23]\. /.test(b.text),
        children: runs(b.text),
      }));
    } else if (b.type === 'h') {
      children.push(slideHeading(b.text));
    } else if (b.type === 'list') {
      instance += 1;
      b.items.forEach((item, k) => children.push(new Paragraph({
        numbering: { reference: b.ordered ? 'numbers' : 'bullets', level: 0, instance },
        spacing: k === 0 && before ? { before, after: 80 } : { after: 80 },
        children: runs(item),
      })));
    } else if ((m = /^\*\*Какво да кажа:\*\* (.*)$/.exec(b.text))) {
      children.push(speechBox(m[1]));
      afterBox = true;
    } else if ((m = /^\*\*На екрана:\*\* (.*)$/.exec(b.text))) {
      children.push(onScreen(m[1]));
    } else if (/^\*\*\d+\. [^*]*\?\*\*$/.test(b.text)) {
      children.push(new Paragraph({
        keepNext: true,
        spacing: { before: 220, after: 60 },
        children: runs(b.text.slice(2, -2), { bold: true, color: NAVY }),
      }));
    } else {
      // „**Ако питат:**“ и подобни надписи остават на една страница със списъка под тях
      const label = /^\*\*[^*]+\*\*( \([^)]*\))?:?$/.test(b.text);
      children.push(new Paragraph({ keepNext: label, spacing: before ? { before } : undefined, children: runs(b.text) }));
    }
  }
  return children;
}

const doc = new Document({
  creator: 'Кръстиян Праматаров',
  title: 'Подготовка за защитата',
  description: 'Ред на защитата, текст към слайдовете и вероятни въпроси с отговори',
  styles: {
    default: {
      document: {
        run: { font: FONT, size: 24, color: INK },
        paragraph: { spacing: { after: 120, line: 288 } },
      },
    },
    paragraphStyles: [
      {
        id: 'Title', name: 'Title', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 40, bold: true, color: NAVY },
        paragraph: { spacing: { after: 160 } },
      },
      {
        id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 32, bold: true, color: NAVY },
        paragraph: { spacing: { before: 360, after: 160 }, keepNext: true, outlineLevel: 0 },
      },
      {
        id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 26, bold: true, color: INK },
        paragraph: { spacing: { before: 360, after: 100 }, keepNext: true, outlineLevel: 1 },
      },
    ],
  },
  numbering: {
    config: [
      {
        reference: 'bullets',
        levels: [{
          level: 0, format: LevelFormat.BULLET, text: '•', alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 420, hanging: 280 } } },
        }],
      },
      {
        reference: 'numbers',
        levels: [{
          level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 420, hanging: 320 } } },
        }],
      },
    ],
  },
  sections: [{
    properties: {
      page: {
        size: { width: PAGE_W, height: PAGE_H },
        margin: { top: MARGIN, right: MARGIN, bottom: MARGIN, left: MARGIN },
      },
    },
    footers: {
      default: new Footer({
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          children: [new TextRun({ children: ['Подготовка за защитата · стр. ', PageNumber.CURRENT], size: 18, color: GREY })],
        })],
      }),
    },
    children: build(parse(fs.readFileSync(SRC, 'utf8'))),
  }],
});

Packer.toBuffer(doc).then((buf) => {
  fs.writeFileSync(OUT, buf);
  console.log('  ✓', path.basename(OUT));
});

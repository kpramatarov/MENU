// Изпълнява скрипта от уеб страницата на главния модул в Node.js с имитация
// на DOM и fetch, като подава реалните JSON отговори от test_master.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const dir = process.argv[2] || '.';
const html = fs.readFileSync(path.join(dir, 'index.html'), 'utf8');
const script = html.slice(html.indexOf('<script>') + 8, html.indexOf('</script>'));

// Всички id от страницата трябва да съществуват, а скриптът да не търси несъществуващи
const htmlIds = new Set([...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]));
let fails = 0;
const fail = (m) => { fails++; console.log('FAIL:', m); };

function makeElement(id) {
  const classes = new Set(id.startsWith('relayBtn') ? ['action-on'] : []);
  return {
    id, innerText: '', className: '', style: {},
    classList: {
      contains: (c) => classes.has(c), add: (c) => classes.add(c), remove: (c) => classes.delete(c),
    },
  };
}

function run(dataFile) {
  const elements = {};
  const document = {
    getElementById(id) {
      if (!htmlIds.has(id)) fail(`скриптът търси несъществуващ елемент #${id}`);
      if (!elements[id]) elements[id] = makeElement(id);
      return elements[id];
    },
  };
  const bodies = {
    '/config': fs.readFileSync(path.join(dir, 'config.json'), 'utf8'),
    '/data': fs.readFileSync(path.join(dir, dataFile), 'utf8'),
  };
  let intervalFn = null;
  const errors = [];
  const ctx = {
    document,
    console,
    alert: (m) => errors.push('alert: ' + m),
    confirm: () => true,
    setInterval: (fn) => { intervalFn = fn; return 1; },
    fetch: (url) => {
      const key = url.split('?')[0];
      if (!(key in bodies)) return Promise.reject(new Error('няма ' + url));
      return Promise.resolve({ json: () => Promise.resolve(JSON.parse(bodies[key])) });
    },
  };
  vm.createContext(ctx);
  vm.runInContext(script, ctx);
  intervalFn();
  return new Promise((resolve) => setTimeout(() => resolve({ elements, errors }), 20));
}

(async () => {
  const full = await run('data_full.json');
  const t = (id) => (full.elements[id] ? full.elements[id].innerText : undefined);
  if (t('temp') !== '22.90') fail('temp = ' + t('temp'));
  if (t('d-cpu') !== '3 µs / 25.59 ms') fail('d-cpu = ' + t('d-cpu'));
  if (t('s-freq') !== 21065) fail('s-freq = ' + t('s-freq'));
  if (!String(t('rtt')).includes('ms')) fail('rtt = ' + t('rtt'));
  if (t('ping') !== '2 / 1') fail('ping = ' + t('ping'));
  if (t('b1') !== '1 / 8 / 5') fail('b1 = ' + t('b1'));
  if (t('status') !== 'Онлайн (преди 0 сек)') fail('status = ' + t('status'));
  if (t('c-fw') !== 'M3 / S3') fail('c-fw = ' + t('c-fw'));
  if (t('c-ssid') !== 'VIVACOM_FiberNet_85C1') fail('c-ssid = ' + t('c-ssid'));
  console.log('UI (данни):', ['temp', 'hr202', 'd-vcc', 'd-cpu', 's-loopmax', 's-freq', 's-period', 'm-loop',
    's-heap', 'ack', 'rx', 'rtt', 'ping', 'b1', 'safety', 'a0'].map(id => `${id}=${t(id)}`).join(' | '));

  const empty = await run('data_empty.json');
  const te = (id) => (empty.elements[id] ? empty.elements[id].innerText : undefined);
  if (te('temp') !== '--' || te('hr202') !== '--') fail('празни данни: ' + te('temp') + ' ' + te('hr202'));
  if (te('ack') !== '-- / -- / --') fail('ack при липса на данни = ' + te('ack'));
  if (!String(empty.elements.banner.innerText).includes('НЯМА ВРЪЗКА')) fail('банер при липса на връзка');

  const proto = await run('data_protowarn.json');
  if (!String(proto.elements.banner.innerText).includes('протокол v2')) fail('банер за версия: ' + proto.elements.banner.innerText);

  const locked = await run('data_locked.json');
  if (locked.elements.hr202.innerText !== 'МОКРО (БЛОКИРАНО)') fail('hr202 = ' + locked.elements.hr202.innerText);

  const nan = await run('data_nan.json');
  if (nan.elements.temp.innerText !== '--') fail('NaN температура = ' + nan.elements.temp.innerText);

  for (const r of [full, empty, proto, locked, nan]) if (r.errors.length) fail(r.errors.join('; '));
  console.log(`UI: ${fails} грешки`);
  process.exit(fails ? 1 : 0);
})();

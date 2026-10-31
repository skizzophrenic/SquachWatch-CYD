// Run the actual picker and tile scripts with a minimal DOM, offline.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../web-flasher/index.html'), 'utf8');
const decode = s => s.replace(/&quot;/g, '"').replace(/&middot;/g, '·')
  .replace(/&mdash;/g, '—').replace(/&amp;/g, '&');
const kit = './manifest-cyd-elecrow-gps.json';

class Element {
  constructor(tagName = 'DIV') {
    this.tagName = tagName.toUpperCase();
    this.dataset = {};
    this.children = [];
    this.listeners = {};
    this.style = {};
    this.attributes = {};
    this.disabled = false;
    this.hidden = false;
    this.classList = { add() {}, toggle() {} };
    this.textContent = '';
  }
  addEventListener(type, fn) { this.listeners[type] = fn; }
  dispatchEvent(event) { if (this.listeners[event.type]) this.listeners[event.type](event); }
  setAttribute(key, value) { this.attributes[key] = value; }
  getAttribute(key) { return this.attributes[key]; }
  appendChild(child) { child.parentNode = this; this.children.push(child); }
  querySelectorAll(selector) {
    return this.children.flatMap(c => [
      ...(selector === '.tile' && /^tile(?: |$)/.test(c.className || '') ? [c] : []),
      ...c.querySelectorAll(selector)
    ]);
  }
}

const select = new Element('SELECT');
select.options = [];
select.selectedIndex = 0;
Object.defineProperty(select, 'value', {
  get() { return this.options[this.selectedIndex]?.value; },
  set(value) { this.selectedIndex = this.options.findIndex(o => o.value === value); }
});
let group;
const optionHtml = html.match(/<select[^>]*id="boardSelect"[\s\S]*?<\/select>/)[0];
for (const match of optionHtml.matchAll(/<optgroup label="([^"]+)"|<option([^>]+)>([\s\S]*?)<\/option>/g)) {
  if (match[1]) { group = { tagName: 'OPTGROUP', label: match[1] }; continue; }
  const o = new Element('OPTION');
  o.parentNode = group;
  o.textContent = decode(match[3]);
  for (const attr of match[2].matchAll(/([\w-]+)="([^"]*)"/g)) {
    if (attr[1] === 'value') o.value = decode(attr[2]);
    if (attr[1].startsWith('data-')) {
      const key = attr[1].slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      o.dataset[key] = decode(attr[2]);
    }
  }
  if (/\sselected(?:\s|$)/.test(match[2])) select.selectedIndex = select.options.length;
  select.options.push(o);
}
const ids = { boardSelect: select };
for (const id of ['installBtn', 'whiteScreenNote', 'elecrowGpsNote', 'versionNote',
  'overclockRow', 'overclockChk', 'overclockDesc', 'versionSelect', 'versionRow',
  'boardCode', 'boardCodeAnswer', 'boardTiles', 'pickedLine', 'clockBtn',
  'clockResult', 'clockDescription', 'clockHelp', 'clockGpsHelp',
  'identifyBtn', 'identifyResult']) ids[id] = new Element();
select.appendChild = option => { option.parentNode = select; select.options.push(option); };
const fetched = [];
let portRequests = 0;
let pendingPort;
const serial = { requestPort: async () => {
  portRequests++;
  if (pendingPort) return pendingPort;
  throw new Error('offline serial checkpoint');
} };
const context = vm.createContext({
  document: {
    getElementById: id => ids[id],
    querySelector: () => new Element(),
    createElement: tag => new Element(tag)
  },
  window: { location: { search: '?lab=1' } },
  navigator: { serial },
  identifyBoard() {},
  fetch: async url => {
    fetched.push(url);
    return { ok: !url.includes('manifest-lab-') || url.includes('lab-cyd-elecrow-gps'),
      status: url.includes('v0.0.1/') ? 404 : 200,
      json: async () => url.endsWith('versions.json') ?
        { versions: [{ dir: '', tag: 'latest' }, { dir: 'v0.0.1/', tag: 'v0.0.1' }] } : { version: '99.1.2' } };
  },
  say() {},
  Event: class { constructor(type) { this.type = type; } },
  MutationObserver: class { observe() {} },
  setInterval() {}
});
const pickerBegin = html.indexOf('  // ---- Board picker ');
assert.ok(pickerBegin > 0);
const pickerEnd = html.indexOf('  })();', pickerBegin) + '  })();'.length;
vm.runInContext(html.slice(pickerBegin, pickerEnd), context);
const clockBegin = html.indexOf("  var identifyBtn = document.getElementById('identifyBtn');");
const clockEnd = html.indexOf("  // The watch's wardrive log as a WigleWifi-1.6 file.", clockBegin);
vm.runInContext(html.slice(clockBegin, clockEnd), context);
const manifest = () => ids.installBtn.attributes.manifest;
const pick = value => { select.value = value; select.dispatchEvent({ type: 'change' }); };

(async () => {
  // Existing default and driver fallback retain their overclock choices.
  assert.equal(manifest(), './manifest-cyd-fast.json');
  assert.equal(ids.clockBtn.disabled, false);
  pick('./manifest-cyd-ili9341.json');
  assert.equal(manifest(), './manifest-cyd-ili9341.json');
  assert.equal(ids.overclockChk.disabled, false);
  ids.overclockChk.checked = true;
  ids.overclockChk.dispatchEvent({ type: 'change' });
  assert.equal(manifest(), './manifest-cyd-ili9341-fast.json');

  pick(kit);
  const accessory = select.options[select.selectedIndex];
  assert.equal(accessory.parentNode.label, 'CHEAP YELLOW DISPLAYS (CYD)');
  assert.equal(accessory.dataset.fast, undefined);
  assert.equal(manifest(), kit);
  assert.equal(ids.overclockChk.checked, false);
  assert.equal(ids.overclockChk.disabled, true);
  assert.equal(ids.elecrowGpsNote.hidden, false);
  assert.equal(ids.whiteScreenNote.hidden, true);
  assert.equal(ids.clockBtn.disabled, true, 'Elecrow must disable application clock commands');
  assert.match(ids.clockDescription.textContent, /GPS.*GPIO1.*console.*disabled/i);
  assert.equal(ids.clockHelp.hidden, true, 'ordinary after-install instructions must be hidden');
  assert.equal(ids.clockGpsHelp.hidden, false);
  assert.match(html.match(/id="clockGpsHelp"[^>]*>([\s\S]*?)<\/p>/)[1], /GPS.*UTC/);
  assert.match(html.match(/id="clockGpsHelp"[^>]*>([\s\S]*?)<\/p>/)[1], /DESK MODE/);
  assert.equal(ids.identifyBtn.disabled, false, 'ROM identification remains available');
  assert.equal(typeof ids.identifyBtn.listeners.click, 'function');
  await context.setBoardClock(); // A direct invocation must also avoid serial access.
  assert.equal(portRequests, 0);
  ids.overclockChk.checked = true; // A stale tick cannot choose ordinary cyd-fast.
  ids.overclockChk.dispatchEvent({ type: 'change' });
  assert.equal(manifest(), kit);

  // The board's code must never infer that this accessory is attached.
  ids.boardCode.value = 'ESP32-2432S028R';
  ids.boardCode.dispatchEvent({ type: 'input' });
  assert.equal(select.value, './manifest-cyd.json');
  assert.equal(manifest(), './manifest-cyd-fast.json');
  assert.equal(ids.elecrowGpsNote.hidden, true);
  assert.equal(ids.clockBtn.disabled, false);
  assert.equal(ids.clockHelp.hidden, false);
  assert.equal(ids.clockGpsHelp.hidden, true);
  assert.match(ids.clockDescription.textContent, /computer's clock and time zone/);
  ids.boardCode.value = 'JC2432W328C';
  ids.boardCode.dispatchEvent({ type: 'input' });
  assert.equal(select.options[select.selectedIndex].dataset.boardId, 'diymalls-jc2432w328c');

  // Actual tile generation must preserve accessory identity and BETA badge.
  const tileScript = html.match(/<script>\s*\/\/ Board tiles[\s\S]*?<\/script>/)[0]
    .replace(/^<script>/, '').replace(/<\/script>$/, '');
  vm.runInContext(tileScript, context);
  const tile = ids.boardTiles.querySelectorAll('.tile').find(t => t.dataset.value === kit);
  assert.ok(tile);
  assert.ok(tile.children.some(c => c.textContent === 'BETA'));
  tile.dispatchEvent({ type: 'click' });
  assert.equal(manifest(), kit);
  assert.equal(ids.elecrowGpsNote.hidden, false);
  assert.match(ids.pickedLine.textContent, /ST7789 \+ Elecrow GPS/);

  // Let versions.json populate before invoking its real change handler.
  await new Promise(resolve => setImmediate(resolve));
  ids.versionSelect.value = 'v0.0.1/';
  ids.versionSelect.dispatchEvent({ type: 'change' });
  assert.equal(manifest(), './v0.0.1/manifest-cyd-elecrow-gps.json');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(ids.installBtn.style.display, 'none');
  assert.match(ids.versionNote.textContent, /not in that version/);
  assert.ok(fetched.includes('./v0.0.1/manifest-cyd-elecrow-gps.json'));
  assert.equal(ids.clockBtn.disabled, true, 'changing release must retain the Elecrow restriction');

  // Lab options arrive asynchronously and must receive the same policy.
  const labKit = './manifest-lab-cyd-elecrow-gps.json';
  assert.ok(select.options.some(o => o.value === labKit));
  pick(labKit);
  assert.equal(ids.clockBtn.disabled, true);
  assert.equal(ids.clockHelp.hidden, true);
  await context.setBoardClock();
  assert.equal(portRequests, 0);

  // Every ordinary option restores the clock tool, including version changes.
  for (let index = 0; index < select.options.length; index++) {
    const option = select.options[index];
    if (option.dataset.elecrowGps === '1') continue;
    select.selectedIndex = index;
    select.dispatchEvent({ type: 'change' });
    assert.equal(ids.clockBtn.disabled, false, option.value);
    assert.equal(ids.clockHelp.hidden, false, option.value);
    assert.equal(ids.clockGpsHelp.hidden, true, option.value);
  }
  await context.setBoardClock();
  assert.equal(portRequests, 1, 'ordinary profiles still reach the serial operation');
  assert.equal(ids.clockBtn.disabled, false);

  // A pending ordinary request cannot re-enable the tool after a profile change.
  let rejectPort;
  pendingPort = new Promise((resolve, reject) => { rejectPort = reject; });
  const setting = context.setBoardClock();
  pick(kit);
  rejectPort(new Error('cancelled offline request'));
  await setting;
  pendingPort = null;
  assert.equal(ids.clockBtn.disabled, true);
  assert.equal(ids.clockHelp.hidden, true);
  pick('./manifest-cyd.json');
  assert.equal(ids.clockBtn.disabled, false);
  delete context.navigator.serial;
  pick(kit);
  pick('./manifest-cyd.json');
  assert.equal(ids.clockBtn.disabled, true, 'browser support restriction is retained');
  console.log('PASS: picker, tiles, releases, production/lab clock restrictions, ordinary restore and pending-operation cleanup');
})().catch(error => { console.error(error); process.exitCode = 1; });

// Developed by X-F1REBALL-X. Host test for the control page script (node):
// runs src/web/index.html against a fake DOM/XHR; reads must be GET, every
// action a POST carrying the page token, a 403 shows the reload hint.
const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8');
const i18n = fs.readFileSync(process.argv[3], 'utf8');
let js = html.match(/<script>([\s\S]*)<\/script>/)[1].replace('/*I18N*/', i18n);
js = js.replace(/HBTOKENx{25}/, 'deadbeefdeadbeefdeadbeefdeadbeef');
const els = {};
function el(id) {
  if (!els[id]) els[id] = { id, style: { setProperty(k, v) { this[k] = v; } }, className: '', textContent: '', innerHTML: '', value: 0,
    attrs: {}, setAttribute(k, v) { this.attrs[k] = v; }, getAttribute: () => null, getElementsByTagName: () => [] };
  return els[id];
}
const calls = [];
class XHR {
  open(m, p) { this.m = m; this.p = p; this.h = {}; }
  setRequestHeader(k, v) { this.h[k] = v; }
  send() { calls.push({ m: this.m, p: this.p, h: this.h }); this.status = 200;
    this.responseText = JSON.stringify(STATUS); if (this.onload) this.onload(); }
}
const STATUS = { version: '1.0.2', state: 'streaming', connected: 1, device: 'X', gain_pct: 500, muted: 0, tone: 0,
  paused: 0, headset_volume: 64, avrcp: { connected: 1 }, pkts: 1, frames: 1, peak: 0, out_peak: 0, sample_rate: 48000,
  bitpool: 35, bitpool_min: 2, bitpool_max: 53, per_packet: 8, backlog: 0, stable: 0, queue_ms: 200, detail: '',
  latency: { target_ms: 200, estimate_ms: 187, capture_ms: 21, packet_ms: 11, queue_ms: 20, radio_ms: 5, sink_ms: 130, sink_reported: 1 },
  codec: 'SBC-XQ', codec_pref: 0, codec_avail: 10, eq: { on: 1, db: [6, 3, 0, 0, 0] } };
const ctx = {
  document: { getElementById: el, querySelectorAll: () => [], documentElement: {} },
  XMLHttpRequest: XHR, localStorage: { getItem: () => 'en', setItem() {} }, navigator: { language: 'en' },
  location: { search: '' }, confirm: () => true, setInterval() {}, setTimeout(fn) { fn(); return 1; }, clearTimeout() {}, Date, JSON, Math, String, parseInt,
};
new Function(...Object.keys(ctx), js)(...Object.values(ctx));
let fails = 0;
const check = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };
check(calls.length && calls[0].m === 'GET' && calls[0].p === '/api/status' && !calls[0].h['X-HB-Token'], 'page: status poll is a plain GET');
const actions = [['mute', '/api/mute?on=1'], ['tone', '/api/tone?on=1'], 
  ['scan', '/api/scan'], ['stop', '/api/stop'], ['clean', '/api/clean']];
for (const [id, path] of actions) {
  calls.length = 0; el(id).onclick();
  const c = calls[0];
  check(c && c.m === 'POST' && c.p === path && c.h['X-HB-Token'] === 'deadbeefdeadbeefdeadbeefdeadbeef', `page: ${id} -> POST ${path} with token`);
}
calls.length = 0; el('gain').value = 250; el('gain').onchange.call(el('gain'));
check(calls[0].m === 'POST' && calls[0].p === '/api/volume?pct=250' && calls[0].h['X-HB-Token'], 'page: volume -> POST with token');
calls.length = 0; el('hs').value = 100; el('hs').onchange.call(el('hs'));
check(calls[0].m === 'POST' && calls[0].p === '/api/headset?vol=100' && calls[0].h['X-HB-Token'], 'page: headset volume -> POST with token');
check(el('latv').textContent === '≈ 187 ms' && el('latt').textContent === '200 ms' && String(el('lat').value) === '200',
  'page: latency meter shows the estimate and the target');
check(/Headset <em>130 ms/.test(el('lkeys').innerHTML) && /Queue <em>20 ms/.test(el('lkeys').innerHTML), 'page: latency breakdown, headset value from its report');
calls.length = 0; el('lat').value = 120; el('lat').onchange.call(el('lat'));
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/latency?ms=120' && calls[0].h['X-HB-Token'], 'page: latency slider -> POST with token');
check(/id="lat" min="60" max="200" step="1"/.test(html), 'page: slider ends at 200 ms in 1 ms steps');
calls.length = 0; el('lat').value = 150; el('lat').oninput.call(el('lat'));
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/latency?ms=150', 'page: moving the slider posts the buffer target');
check(html.indexOf('id="evbox"') < html.indexOf('id="devlist"') && html.indexOf('id="evbox"') > html.indexOf('data-i18n="found"'),
  'page: recent log sits in Found nearby');
check(html.indexOf('id="evbox"') < html.indexOf('data-i18n="status"'), 'page: recent log is not in the status panel');
check(el('cd0').className === 'act' && el('cd2').disabled && el('cd2').className === 'no' && !el('cd3').disabled,
  'page: codec picker shows auto, hides what the headset cannot take');
check(/Not supported by this headset: SBC HQ/.test(el('codecno').innerHTML), 'page: says which codecs this headset does not support');
calls.length = 0; el('cd2').onclick.call(el('cd2'));
check(calls.length === 0, 'page: an unsupported codec cannot be picked');
calls.length = 0; el('cd3').onclick.call(el('cd3'));
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/codec?mode=3' && calls[0].h['X-HB-Token'], 'page: codec -> POST with token');
check(el('pr1').className === 'act' && el('eqv0').textContent === '+6 dB' && /^M0\.0 /.test(el('eqline').attrs.d || ''),
  'page: EQ shows the bass boost preset, values and a curve');
calls.length = 0; el('pr4').onclick();
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/eq?on=1&b0=-3&b1=-1&b2=2&b3=5&b4=2' && calls[0].h['X-HB-Token'],
  'page: voice/footsteps preset -> POST /api/eq with token');
calls.length = 0; el('eqon').onclick();
check(calls[0] && /^\/api\/eq\?on=0&/.test(calls[0].p), 'page: EQ toggle -> on=0');
// 403 (HearBridge restarted with a new token) -> reload hint
XHR.prototype.send = function () { calls.push({}); this.status = 403; this.responseText = '{"error":"token"}'; this.onload(); };
el('mute').onclick();
check(el('msg').textContent.indexOf('Reload') >= 0, 'page: 403 shows the reload hint');
XHR.prototype.send = function () { calls.push({ m: this.m, p: this.p, h: this.h }); this.status = 200;
  this.responseText = JSON.stringify(STATUS); if (this.onload) this.onload(); };
STATUS.avrcp = { connected: 0, absolute_volume: 1, notifications: 1, sink_volume: 1 };
STATUS.headset_volume = 123;
STATUS.xq_low = 1;
STATUS.events = ['switch: now SBC', 'stream: link dropped'];
global.hbTest.req('/api/status');
check(/absolute volume/.test(el('av').innerHTML) && !/not connected/.test(el('av').innerHTML),
  'page: AVRCP says connected when the headset is reporting volume');
check(el('hv').textContent === '97%', 'page: headset volume 123/127 is 97%');
check(/low bitpool/.test(el('xqnote').innerHTML), 'page: says plain SBC will sound better when XQ stays low');
check(/link dropped/.test(el('evlog').innerHTML) && el('evbox').style.display === '', 'page: recent switches and disconnects');
STATUS.state = 'disconnected'; STATUS.connected = 0; STATUS.why = 'dropped';
global.hbTest.req('/api/status');
check(/headset dropped the link/.test(el('state').textContent), 'page: shows why the headset disconnected');
console.log(fails ? `FAILED (${fails})` : 'ALL OK (0 failures)');
process.exit(fails ? 1 : 0);

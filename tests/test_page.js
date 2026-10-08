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
  if (!els[id]) els[id] = { id, style: {}, className: '', textContent: '', innerHTML: '', value: 0,
    getAttribute: () => null, getElementsByTagName: () => [] };
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
  codec: 'SBC-XQ', codec_pref: 0, codec_avail: 10 };
const ctx = {
  document: { getElementById: el, querySelectorAll: () => [], documentElement: {} },
  XMLHttpRequest: XHR, localStorage: { getItem: () => 'en', setItem() {} }, navigator: { language: 'en' },
  location: { search: '' }, confirm: () => true, setInterval() {}, Date, JSON, Math, String, parseInt,
};
new Function(...Object.keys(ctx), js)(...Object.values(ctx));
let fails = 0;
const check = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };
check(calls.length && calls[0].m === 'GET' && calls[0].p === '/api/status' && !calls[0].h['X-HB-Token'], 'page: status poll is a plain GET');
const actions = [['mute', '/api/mute?on=1'], ['tone', '/api/tone?on=1'], ['lat', '/api/latency?stable=1'],
  ['scan', '/api/scan'], ['stop', '/api/stop']];
for (const [id, path] of actions) {
  calls.length = 0; el(id).onclick();
  const c = calls[0];
  check(c && c.m === 'POST' && c.p === path && c.h['X-HB-Token'] === 'deadbeefdeadbeefdeadbeefdeadbeef', `page: ${id} -> POST ${path} with token`);
}
calls.length = 0; el('gain').value = 250; el('gain').onchange.call(el('gain'));
check(calls[0].m === 'POST' && calls[0].p === '/api/volume?pct=250' && calls[0].h['X-HB-Token'], 'page: volume -> POST with token');
calls.length = 0; el('hs').value = 100; el('hs').onchange.call(el('hs'));
check(calls[0].m === 'POST' && calls[0].p === '/api/headset?vol=100' && calls[0].h['X-HB-Token'], 'page: headset volume -> POST with token');
check(el('lat').textContent === 'Low latency', 'page: latency button shows the mode');
check(el('cd0').className === 'act' && el('cd2').disabled && !el('cd3').disabled, 'page: codec picker shows auto, greys out what the sink lacks');
calls.length = 0; el('cd3').onclick.call(el('cd3'));
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/codec?mode=3' && calls[0].h['X-HB-Token'], 'page: codec -> POST with token');
// 403 (HearBridge restarted with a new token) -> reload hint
XHR.prototype.send = function () { calls.push({}); this.status = 403; this.responseText = '{"error":"token"}'; this.onload(); };
el('mute').onclick();
check(el('msg').textContent.indexOf('Reload') >= 0, 'page: 403 shows the reload hint');
console.log(fails ? `FAILED (${fails})` : 'ALL OK (0 failures)');
process.exit(fails ? 1 : 0);

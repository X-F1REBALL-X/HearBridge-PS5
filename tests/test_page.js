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
    attrs: {}, setAttribute(k, v) { this.attrs[k] = v; }, getAttribute: () => null, getElementsByTagName: () => [], parentNode: null,
    appendChild(c) { c.parentNode = this; } };
  return els[id];
}
const calls = [];
class XHR {
  open(m, p) { this.m = m; this.p = p; this.h = {}; }
  setRequestHeader(k, v) { this.h[k] = v; }
  send() { calls.push({ m: this.m, p: this.p, h: this.h }); this.status = 200;
    this.responseText = JSON.stringify(STATUS); if (this.onload) this.onload(); }
}
const STATUS = { version: '1.1.0', state: 'streaming', connected: 1, device: 'X', gain_pct: 500, muted: 0, tone: 0,
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
check(calls.some(function(c,i){return i>0 && c.m==='POST' && c.p==='/api/scan' && c.h['X-HB-Token']}), 'page: refresh starts a scan');
check(/setInterval\(function\(\)\{if\(scanLive/.test(html), 'page: devices are polled during the scan, not after it');
check(/Nothing yet/.test(el('evlog').innerHTML), 'page: empty log says nothing yet');
check(/if\(lg\.hbFollow\)lg\.scrollTop=lg\.scrollHeight/.test(html) && /this\.hbFollow=this\.scrollTop/.test(html), 'page: log follows the newest line unless scrolled up');
check(/patchList\('devlist'/.test(html) && /patchList\('savedlist'/.test(html) && !/\$\('(devlist|savedlist)'\)\.innerHTML=/.test(html),
  'page: device lists are patched in place (no rebuild per poll, no hover/focus blink)');
const actions = [['mute', '/api/mute?on=1'], ['tone', '/api/tone?on=1'], 
  ['stop', '/api/stop'], ['clean', '/api/clean']];
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
check(/id="lat" min="40" max="200" step="1"/.test(html), 'page: slider goes 40-200 ms in 1 ms steps');
calls.length = 0; el('lat').value = 150; el('lat').oninput.call(el('lat'));
check(calls[0] && calls[0].m === 'POST' && calls[0].p === '/api/latency?ms=150', 'page: moving the slider posts the buffer target');
{
  const main0 = html.slice(html.indexOf('<main'), html.indexOf('</main>'));
  check(main0.indexOf('class="panel logp"') > 0 && main0.indexOf('id="evlog"') > 0 && main0.indexOf('class="panel status"') < 0 &&
    main0.slice(main0.indexOf('id="hspanel"')).indexOf('id="fmt"') > 0 &&
    html.indexOf('data-i18n="logEmpty"') > 0, 'page: status merged into the Headset panel, log on the main screen');
  check(main0.indexOf('class="panel conn"') > 0 && main0.indexOf('id="codecs"') > 0 && main0.indexOf('id="stop"') > 0,
    'page: connection (codec, chip, stop) is on the main screen');
  const hdr = html.slice(html.indexOf('<header'), html.indexOf('</header>'));
  check(hdr.indexOf('id="setbtn"') >= 0 && hdr.indexOf('id="setbtn"') < hdr.indexOf('class="brand"') &&
    /class="drawer" id="setm"/.test(html), 'page: settings button top left, opens a drawer');
}
{
  const main = html.slice(html.indexOf('<main'), html.indexOf('</main>'));
  check(main.indexOf('id="savedlist"') < 0 && main.indexOf('id="devlist"') < 0 &&
        main.indexOf('id="bkdo"') < 0 && main.indexOf('id="gsave"') < 0,
        'page: device lists, game profiles and backup stay in Settings');
  check(main.indexOf('id="eqp"') < main.indexOf('id="eq0"') && main.indexOf('id="eq4"') > 0 && main.indexOf('id="eqline"') > 0,
        'page: the full equalizer (5 bands + curve) is on the main screen, in Sound');
  const c1 = main.slice(main.indexOf('class="col c1"'), main.indexOf('class="col c2"'));
  const c3 = main.slice(main.indexOf('class="col c3"'));
  check(c1.indexOf('class="panel conn"') > 0 && c1.indexOf('id="codecnote"') < 0 && /id="stop" class="red sm"/.test(c1),
        'page: connection is compact (codec, chip, small stop button)');
  const c2 = main.slice(main.indexOf('class="col c2"'), main.indexOf('class="col c3"'));
  check(c3.indexOf('id="hspanel"') > 0 && c2.indexOf('id="eqp"') >= 0 && c2.indexOf('id="gain"') > c2.indexOf('id="eqp"') &&
        c1.indexOf('class="panel conn"') < c1.indexOf('class="panel logp"') && c1.indexOf('id="lat"') > c1.indexOf('class="panel conn"'),
        'page: 4 panels: headset (with status) | sound (with volume) | connection (with latency), log');
  check((main.match(/<section class="panel/g) || []).length === 4, 'page: main screen has 4 panels');
  check(/class="side"><p class="navgrp"/.test(html) && /class="dmain"/.test(html) && /id="pgtitle"/.test(html),
        'page: settings side menu (categories left, page right)');
  check(main.indexOf('id="qs"') > 0 && main.indexOf('id="batm"') > 0 && main.indexOf('id="gain"') > 0 && main.indexOf('id="hs"') > 0 &&
        main.indexOf('id="lat"') > 0 && main.indexOf('id="latv"') > 0 && main.indexOf('id="pr0"') > 0 && main.indexOf('id="night"') > 0,
        'page: main screen has headset switch, battery, volume, latency + estimate, EQ presets and night mode');
  check(main.indexOf('id="pk"') < 0 && main.indexOf('id="av"') < 0 && main.indexOf('id="m1"') < 0 && main.indexOf('id="lbar"') < 0 &&
        main.indexOf('id="fmt"') > 0 && main.indexOf('id="batm"') > 0 && main.indexOf('id="lqv"') > 0,
        'page: headset shows format, battery, link; counters, AVRCP, peaks and latency breakdown are in Settings');
  {
    const pg4 = html.slice(html.indexOf('id="pg2"'));
    check(/id="tb2"[^>]*><svg[\s\S]*?data-i18n="tabDetails"/.test(html) && !/id="tb4"/.test(html) && pg4.indexOf('id="pk"') > 0 && pg4.indexOf('id="av"') > 0 && pg4.indexOf('id="m2"') > 0 && pg4.indexOf('id="lkeys"') > 0,
          'page: Settings > Details has the counters, AVRCP, peaks and latency breakdown');
    check(pg4.indexOf('id="chipv"') > 0 && pg4.indexOf('id="latnote"') > 0 && pg4.indexOf('id="fmtd"') > 0 && main.indexOf('id="chipv"') < 0,
          'page: chip, latency hint and full format details live in Settings > Details');
  }
  check(!/[\u2013\u2014]/.test(html) && !/[\u2013\u2014]/.test(i18n), 'page: no em / en dashes anywhere the user reads');
}
check(html.indexOf('id="reset"') < 0 && html.indexOf('/api/reset') < 0 &&
  html.indexOf('id="reconnect"') < 0 && /t\('reconnect'\)/.test(html),
  'page: no big Reconnect/Reset under Scan, per-row Reconnect kept');
check(el('cd0').textContent === 'Auto · SBC-XQ' && el('cd3').className === 'cur' && /Auto picked SBC-XQ/.test(el('codecno').innerHTML),
  'page: auto shows the codec it is streaming');
check(el('cd0').className === 'act' && el('cd2').disabled && el('cd2').className === 'no' && !el('cd3').disabled && el('cd3').className !== 'no',
  'page: auto is on, a supported codec stays clickable, an unsupported one is grey');
check(/\.seg button\[disabled\],\.seg button\.no\{color:var\(--mute\)/.test(html) && !/\.seg button\[disabled\],\.seg button\.no\{display:none\}/.test(html),
  'page: unsupported codecs stay visible and grey, not hidden');
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
global.hbTest.draw && global.hbTest.draw();
check(el('msg').textContent.indexOf('Reload') >= 0 || !global.hbTest.draw, 'page: the reload hint survives the next status draw');
check(/\.msg\{position:fixed;z-index:(9[1-9]|[1-9]\d\d)/.test(html) && /\.drawer\{position:fixed;z-index:90/.test(html),
  'page: the error hint shows above the Settings overlay');
// Page left open over a restart: the old token gets 403, the status carries the
// new one, the press is sent again with it and goes through.
{
  const NEW = '0123456789abcdef0123456789abcdef', seen = [];
  XHR.prototype.send = function () {
    seen.push({ m: this.m, p: this.p, t: this.h['X-HB-Token'] });
    if (this.m === 'POST' && this.h['X-HB-Token'] !== NEW) { this.status = 403; this.responseText = '{"error":"token"}'; }
    else { this.status = 200; this.responseText = this.m === 'GET' ? JSON.stringify(Object.assign({}, STATUS, { token: NEW })) : '{"ok":1}'; }
    this.onload();
  };
  el('tone').onclick();
  const posts = seen.filter(c => c.m === 'POST');
  check(posts.length === 2 && posts[0].t === 'deadbeefdeadbeefdeadbeefdeadbeef' && posts[1].t === NEW && /^\/api\/tone/.test(posts[1].p) &&
        seen.some(c => c.m === 'GET' && c.p === '/api/status'), 'page: a 403 takes the new token from the status and sends the press again');
  check(el('msg').textContent === '', 'page: the reload hint goes away once a press goes through');
  seen.length = 0; el('mute').onclick();
  check(seen.length === 1 && seen[0].t === NEW, 'page: later presses use the new token right away');
  ['tb0','tb1','tb2','tb3','tbhome'].forEach(id => { if (!el(id).focus) el(id).focus = function () {}; });
  // streaming: Scan / Add headset do not scan (shared radio with the DualSense), they say why
  const toastLog = []; { let v = ''; Object.defineProperty(el('toast'), 'textContent', { get: () => v, set: (x) => { v = x; if (x) toastLog.push(x); } }); }
  STATUS.state = 'streaming'; STATUS.device = 'Xbox Wireless Headset'; global.hbTest.req('/api/status');
  seen.length = 0; toastLog.length = 0; el('scan').onclick();
  check(!seen.some(c => c.m === 'POST') && toastLog.pop() === 'Disconnect Xbox Wireless Headset first to add a new headset',
        'page: Scan while streaming does not scan, says to disconnect the headset first');
  seen.length = 0; toastLog.length = 0; el('addhs').onclick();
  check(!seen.some(c => c.m === 'POST') && /^Disconnect Xbox/.test(toastLog.pop() || ''), 'page: Add headset while streaming says the same');
  check(/\.toast\{position:fixed;z-index:(9[1-9]|[1-9]\d\d)/.test(html), 'page: that note shows above the Settings overlay');
  STATUS.state = 'disconnected'; global.hbTest.req('/api/status');
  seen.length = 0; el('scan').onclick();
  check(seen.some(c => c.m === 'POST' && c.p === '/api/scan?user=1'), 'page: Scan with no headset streaming asks for a real scan (user=1)');
  seen.length = 0; el('addhs').onclick();
  check(seen.some(c => c.p === '/api/scan?user=1'), 'page: Add headset asks for a real scan (user=1)');
  STATUS.state = 'streaming';
}
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
check(el('hvnote').textContent === 'Sets the headset volume' && /Sets the headset volume/.test(el('av').innerHTML) && !/Follows/.test(el('av').innerHTML),
  'page: headset never moved the volume itself: slider says it sets the headset volume (one way)');
STATUS.hs_moves = 1; global.hbTest.req('/api/status');
check(el('hvnote').textContent === 'Follows the headset', 'page: once the headset moved it, the slider says it follows the headset');
STATUS.hs_moves = 0; STATUS.avrcp = { connected: 1, absolute_volume: 0, notifications: 0, sink_volume: 0 }; global.hbTest.req('/api/status');
check(el('hvnote').textContent === 'Software volume', 'page: software volume says so');
STATUS.avrcp = { connected: 0, absolute_volume: 1, notifications: 1, sink_volume: 1 }; global.hbTest.req('/api/status');
check(/low bitpool/.test(el('xqnote').innerHTML), 'page: says plain SBC will sound better when XQ stays low');
check(/link dropped/.test(el('evlog').innerHTML), 'page: recent switches and disconnects');
STATUS.state = 'disconnected'; STATUS.connected = 0; STATUS.why = 'dropped';
global.hbTest.req('/api/status');
check(/headset dropped the link/.test(el('state').textContent), 'page: shows why the headset disconnected');
// Device row after a drop: the status says disconnected -> row not connected at once.
const B = () => global.hbTest.badge(1);
STATUS.state = 'streaming'; STATUS.connected = 1; STATUS.why = '';
global.hbTest.req('/api/status');
check(/>Connected</.test(B()), 'page: saved row shows connected while streaming');
STATUS.state = 'disconnected'; STATUS.connected = 0; STATUS.why = 'away';
global.hbTest.req('/api/status');
check(!/>Connected</.test(B()), 'page: saved row shows not connected on the first poll after the drop');
// Stale status (no answer for > 4 s): never keep "connected".
STATUS.state = 'streaming'; STATUS.connected = 1;
global.hbTest.req('/api/status');
const realNow = Date.now;
Date.now = () => realNow() + 10000;
global.hbTest.draw();
check(!/>Connected</.test(B()) && el('dev').textContent !== 'X',
  'page: an old status (tab asleep, no answer) does not keep showing connected');
Date.now = realNow;
// Bluetooth chip row (one build for every chip: no MediaTek hint any more)
STATUS.chip = { vid: '', pid: '', vendor: '', mediatek: 0, profile: '' };
global.hbTest.req('/api/status');
check(el('chipv').textContent === '-', 'page: chip row is "-" before the controller is open');
STATUS.chip = { vid: '1286', pid: '2059', vendor: 'Marvell/NXP', mediatek: 0, profile: 'default' };
global.hbTest.req('/api/status');
check(el('chipv').textContent === 'Marvell/NXP (1286:2059)', 'page: Marvell chip');
STATUS.chip = { vid: 'abcd', pid: '0012', vendor: '', mediatek: 0, profile: 'default' };
global.hbTest.req('/api/status');
check(el('chipv').textContent === 'abcd:0012', 'page: unknown chip shows only the IDs');
STATUS.chip = { vid: '0e8d', pid: '3605', vendor: 'MediaTek', mediatek: 1, profile: 'mediatek' };
global.hbTest.req('/api/status');
check(el('chipv').textContent === 'MediaTek (0e8d:3605)', 'page: MediaTek chip');
// battery: level when the headset reports it, "—" when it does not
STATUS.battery = { status: '', level: -1 };
global.hbTest.req('/api/status');
check(el('batv').innerHTML === '-', 'page: battery shows "-" when unknown');
STATUS.battery = { status: 'low', level: 20, pct: -1, none: 0 };
global.hbTest.req('/api/status');
check(/Low/.test(el('batv').innerHTML) && !/calc\(/.test(el('batv').innerHTML), 'page: AVRCP only: state text, no fake percent gauge');
STATUS.battery = { status: 'ok', level: 60, pct: -1, none: 0 };
global.hbTest.req('/api/status');
check(/OK/.test(el('batv').innerHTML) && !/60/.test(el('batv').innerHTML), 'page: AVRCP "ok" is not shown as 60%');
STATUS.battery = { status: '', level: -1, pct: 70, none: 0 };
global.hbTest.req('/api/status');
check(/calc\(70%/.test(el('batv').innerHTML) && />70%</.test(el('batv').innerHTML), 'page: HFP percent with gauge');
STATUS.battery = { status: '', level: -1, pct: 8, none: 0 };
global.hbTest.req('/api/status');
check(/bat crit/.test(el('batv').innerHTML), 'page: HFP low percent is red');
STATUS.battery = { status: '', level: -1, pct: -1, none: 1 };
global.hbTest.req('/api/status');
check(/Not shown by this headset/.test(el('batm').innerHTML), 'page: nothing reported: "Not shown by this headset"');
STATUS.battery = { status: '', level: -1, pct: -1, none: 0 };
global.hbTest.req('/api/status');
// games: main screen only shows that a game's sound is on; Settings has save / remove
STATUS.game = { avail: 1, id: '', name: '', profile: 0, active: 0 };
global.hbTest.req('/api/status');
check(el('gline').style.display === 'none' && el('gsave').disabled === true && el('gname').textContent === 'No game running',
  'page: no game: nothing on the main screen, save is off');
check(el('gupd').disabled === true, 'page: no game: Update Game Profile not offered');
STATUS.game = { avail: 1, id: 'PPSA01325', name: 'ASTRO BOT', profile: 0, active: 0, dirty: 1 };
global.hbTest.req('/api/status');
check(el('gline').style.display === '' && /No profile for ASTRO BOT on this headset yet/.test(el('gltxt').textContent) &&
  el('gname').textContent === 'ASTRO BOT' && !el('gsave').disabled && el('gdrop').style.display === 'none' && el('gupd').disabled === false,
  'page: game without a profile: the game line says so, it can be saved, nothing to remove');
STATUS.game = { avail: 1, id: 'PPSA01325', name: 'ASTRO BOT', profile: 1, active: 1, exact: 1, from: '', dirty: 0,
  saved: [{ id: 'PPSA01325', name: 'ASTRO BOT', hs: [{ a: 'D8:E2:DF:F7:D7:44', n: 'Xbox Wireless Headset', cur: 1 }, { a: '58:18:62:63:3B:7C', n: 'WF-1000XM6', cur: 0 }] },
          { id: 'CUSA00001', name: 'Other', hs: [] }] };
global.hbTest.req('/api/status');
check(el('gline').style.display === '' && /^Game sound on for ASTRO BOT$/.test(el('gltxt').textContent) && el('gdrop').style.display === '',
  'page: game sound on: main screen says so, Settings can remove it');
check(el('gupd').disabled === true && el('gupd').className === 'gup', 'page: Update Game Profile is grey while the sound matches the saved profile');
check(el('gsave').textContent === 'Update Game Profile', 'page: Settings button says Update Game Profile once this headset has one');
calls.length = 0; el('gupd').onclick.call(el('gupd'));
check(calls.length === 0, 'page: grey Update Game Profile does nothing');
STATUS.game.dirty = 1;
global.hbTest.req('/api/status');
check(el('gupd').disabled === false && /\bon\b/.test(el('gupd').className), 'page: Update Game Profile turns active once the sound is changed');
STATUS.game.dirty = 0;   /* what the server answers the save with */
calls.length = 0; el('gupd').onclick.call(el('gupd'));
check(calls[0] && calls[0].p === '/api/game?do=1' && calls[0].m === 'POST' && el('gupd').disabled === true,
  'page: Update Game Profile saves for this game and headset, then goes grey');
STATUS.game.dirty = 0; STATUS.game.exact = 0; STATUS.game.from = 'WF-1000XM6';
global.hbTest.req('/api/status');
check(/ASTRO BOT \(from WF-1000XM6\)/.test(el('gltxt').textContent), 'page: says when the profile comes from another headset');
{
  const sv = STATUS.game.saved;
  STATUS.game.saved = [{ id: 'PPSA01325', name: 'ASTRO BOT', hs: [{ a: '58:18:62:63:3B:7C', n: 'WF-1000XM6', cur: 0 }] }];
  global.hbTest.req('/api/status');
  check(el('gdrop').style.display === 'none', 'page: borrowing another headset profile: Remove is hidden (it would not be ours to delete)');
  STATUS.game.saved = sv; global.hbTest.req('/api/status');
}
{
  const gl = el('glist').innerHTML;
  check(/class="hchip cur"><button type="button" data-gpick="PPSA01325" data-hs="D8:E2:DF:F7:D7:44"/.test(gl) && /Xbox Wireless Headset/.test(gl) &&
    /data-ghdel="PPSA01325" data-hs="58:18:62:63:3B:7C"/.test(gl) && /<svg viewBox/.test(gl), 'page: game card shows a chip with icon and name per saved headset');
  check(/Not saved for a headset yet/.test(gl), 'page: a game with no headset profile says so');
  check(/data-gpick="PPSA01325" data-hs="58:18:62:63:3B:7C" title="Use the WF-1000XM6 profile now"/.test(gl), 'page: chips of the running game can be picked');
}
check(/act\('\/api\/game\?do=5&id='/.test(html) && /act\('\/api\/game\?do=4&id='/.test(html) && /gameHsRemoveConfirm/.test(html),
  'page: pick (use now) and delete per headset');
check(/@media \(max-width:560px\)\{button\.gup\{flex:1 1 100%/.test(html) && /button\.gup\{[^}]*min-height:2\.6rem[^}]*white-space:nowrap/.test(html),
  'page: Update Game Profile is a big button that wraps to its own line on phones');
check(el('gsave').disabled === true, 'page: Settings Update Game Profile is grey too while nothing changed');
STATUS.game.dirty = 1; global.hbTest.req('/api/status');
calls.length = 0; el('gsave').onclick.call(el('gsave'));
check(calls[0] && calls[0].p === '/api/game?do=1' && calls[0].m === 'POST', 'page: save for this game -> POST');
// night mode
STATUS.night = { on: 0, db10: 0 };
global.hbTest.req('/api/status');
calls.length = 0; el('night').onclick();
check(calls[0] && calls[0].p === '/api/night?on=1' && calls[0].h['X-HB-Token'] && el('night').className === 'night',
  'page: night mode toggle -> POST on=1');
STATUS.night = { on: 1, db10: 60 };
global.hbTest.req('/api/status');
check(el('night').className === 'night on', 'page: night mode shows on');
// low battery toast text (the fake setTimeout clears it at once, so check the code path)
check(/toast\(t\('battToast'\)\.replace\('%s',ba\.level\)\)/.test(html) && /hb_batt_seq/.test(html), 'page: low battery toast, once per heads-up');
// merged latency slider: "Latency" label, ms value next to it, meter under it
{
  const m = html.match(/<div class="latm">([\s\S]*?)<div class="lkeys"/)[1];
  const lp = html.slice(html.indexOf('class="latp latin"'));
  check(lp.indexOf('data-i18n="latency"') > 0 && m.indexOf('id="lat"') < m.indexOf('id="latv"') && m.indexOf('id="latt"') > m.indexOf('id="lat"'),
    'page: Latency (in Connection), slider with its ms value, live meter under it');
  check(!/llseg|latMode|\/api\/lowlat/.test(html), 'page: no separate low latency mode');
}
{
  const fs = require('fs');
  const html = fs.readFileSync(require('path').join(__dirname, '..', 'src', 'web', 'index.html'), 'utf8');
  const i18n = fs.readFileSync(require('path').join(__dirname, '..', 'src', 'web', 'i18n.json'), 'utf8');
  check(!/mtkhint|mtk-test|mtk_build/i.test(html) && !/mtkHint/.test(i18n), 'page: no link to a separate mediatek build');
}
{
  const i18n = JSON.parse(fs.readFileSync(require('path').join(__dirname, '..', 'src', 'web', 'i18n.json'), 'utf8'));
  check(!i18n.langs.some(l => l.code === 'he') && !i18n.strings.he && i18n.langs.length === 10 && !/[\u0590-\u05ff]/.test(html),
    'page: no Hebrew (language list, strings, text)');
  const de = {}; let saved = null;
  const c2 = Object.assign({}, ctx, { document: { getElementById: el, querySelectorAll: () => [], documentElement: de },
    localStorage: { getItem: () => 'he', setItem(k, v) { saved = v; } } });
  new Function(...Object.keys(c2), js)(...Object.values(c2));
  check(de.lang === 'en' && de.dir === 'ltr' && saved === 'en', 'page: a saved he preference falls back to English');
}
check(/src="\/api\/gameicon\?id='\+encodeURIComponent\(id\)/.test(html) && /onerror="this\.parentNode\.removeChild\(this\)"/.test(html) &&
  /<i>'\+esc\(L\)/.test(html) && /id="glist"/.test(html), 'page: games show the console icon, letter tile when missing');
check(/act\('\/api\/game\?do=3&id='\+encodeURIComponent\(id\)\)/.test(html) && /gListSig/.test(html), 'page: saved games list, remove by id, redrawn only on change');
{
  const bc = (html.match(/class="band" style="--c:var\(--(\w+)\)"/g) || []).map(x => x.replace(/.*--(\w+)\).*/, '$1'));
  check(bc.length === 5 && new Set(bc).size === 5 && /EQCOL=\['#ff6a2b','#ffb23d','#c8ff3d','#3dd8ff','#ff7ad9'\]/.test(html),
    'page: each EQ band has its own color (slider, fill, value)');
  check(/\.c3\{order:1\}\.c2\{order:2\}\.c1\{order:3\}/.test(html), 'page: TV columns: status/headset/log left, sound middle, volume/latency/connection right');
}
{
  const pg1 = html.slice(html.indexOf('id="pg1"'), html.indexOf('id="pg3"'));
  check((html.match(/id="tb\d"/g) || []).length === 4 && pg1.indexOf('id="gsave"') > 0 && pg1.indexOf('id="tone"') > 0,
    'page: settings has 4 categories, games live in Sound & games');
}
// settings button: inset from the corner, big hit area, closed drawer never takes clicks
check(/#setbtn\{min-height:3\.6rem;min-width:13rem;[^}]*margin-inline-start:2\.6rem/.test(html) && /\.bar\{padding-top:1\.6rem\}/.test(html),
    'page: TV settings button inset from the screen edge and bigger');
check(/visibility:hidden;pointer-events:none/.test(html) && /\.drawer\.show\{[^}]*pointer-events:auto/.test(html) && /\.scrim\{[^}]*pointer-events:none/.test(html),
    'page: closed drawer and scrim do not catch clicks');
check(/getGamepads/.test(html) && /buttons\[9\]/.test(html) && /ContextMenu/.test(html), 'page: Options / menu key opens Settings');
// just-left headset that will not answer pages: ask for a power cycle (state chip)
STATUS.why = 'powercycle'; STATUS.state = 'disconnected';
global.hbTest.req('/api/status');
check(/Turn the headset off and on to reconnect/.test(el('state').textContent), 'page: power cycle hint when a just-left headset does not answer');
STATUS.why = ''; STATUS.state = 'streaming';
global.hbTest.req('/api/status');
check(/\.by\{color:#fff;/.test(html), 'page: credit line under the name is white');
// Home category replaces the Close button
check(!/id="setclose"/.test(html) && /<nav class="tabs" id="tabs">\n<button type="button" id="tbhome"/.test(html) && /\$\('tbhome'\)\.onclick=setClose/.test(html),
    'page: Home at the top of the settings menu closes it, no Close button');
check(/\.drawer\{width:100vw;grid-template-columns:22rem/.test(html) && /\.pages\{zoom:1\.35/.test(html), 'page: TV settings is a large overlay with bigger content');
{
  const eq = html.slice(html.indexOf('id="eqp"'));
  check(eq.indexOf('id="eqon"') < eq.indexOf('id="gain"') && eq.indexOf('id="pr0"') < eq.indexOf('id="gain"') && eq.indexOf('id="hs"') < eq.indexOf('id="night"') &&
    eq.indexOf('id="mute"') < eq.indexOf('id="eqon"'), 'page: Sound panel: EQ first, then volume, then night mode; mute in the header');
}
check(/@font-face\{font-family:"HB Inter";src:url\(\/font\.woff2\)/.test(html) && /font:1rem\/1\.35 "HB Inter","Bahnschrift"/.test(html) &&
  /\.brand h1\{font-family:"Bahnschrift"/.test(html) && !/https?:\/\/[^"']*\.(woff2?|ttf)/.test(html), 'page: local Inter font with system fallback, title keeps its font');
console.log(fails ? `FAILED (${fails})` : 'ALL OK (0 failures)');
process.exit(fails ? 1 : 0);

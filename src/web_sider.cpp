#include "web_sider.h"

namespace web_sider {

// Fælles udseende. Holdes lille; det fulde UI kommer i fase 7.
#define IHC_CSS                                                                                              \
  "<style>"                                                                                                  \
  ":root{--bg:#f4f5f7;--kort:#fff;--tekst:#1c1f24;--daem:#5b6270;--accent:#0b6bcb;--ok:#1a7f37;"            \
  "--fejl:#c0362c;--kant:#dde1e6;--til:#f5b301}"                                                             \
  "@media(prefers-color-scheme:dark){:root{--bg:#121417;--kort:#1c1f24;--tekst:#e8eaed;--daem:#9aa1ad;"      \
  "--kant:#2d323a;--accent:#4c9aff}}"                                                                        \
  "*{box-sizing:border-box}body{margin:0;font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;" \
  "background:var(--bg);color:var(--tekst)}main{max-width:960px;margin:0 auto;padding:16px}"                \
  "h1{font-size:1.4rem;margin:8px 0 16px}h2{font-size:1.1rem;margin:0 0 12px}"                               \
  ".kort{background:var(--kort);border:1px solid var(--kant);border-radius:12px;padding:16px;margin:0 0 16px}" \
  "label{display:block;margin:12px 0 4px;color:var(--daem);font-size:.9rem}"                                 \
  "input,select{width:100%;padding:10px;border:1px solid var(--kant);border-radius:8px;background:var(--bg);" \
  "color:var(--tekst);font:inherit}button{padding:10px 16px;border:0;border-radius:8px;background:var(--accent);" \
  "color:#fff;font:inherit;cursor:pointer}button.sek{background:transparent;color:var(--accent);"            \
  "border:1px solid var(--kant)}button:disabled{opacity:.5}.raekke{display:flex;gap:8px;align-items:end}"   \
  ".raekke>*{flex:1}.raekke>button{flex:0 0 auto}.besked{margin:12px 0 0;min-height:1.4em}"                  \
  ".fejl{color:var(--fejl)}.ok{color:var(--ok)}.daem{color:var(--daem);font-size:.9rem}"                      \
  "</style>"

const char kOpsaetning[] = R"HTML(<!doctype html>
<html lang="da"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IHC Bridge – Opsætning</title>)HTML" IHC_CSS R"HTML(</head><body><main>
<h1>IHC Bridge – Opsætning</h1>

<form id="form" class="kort" autocomplete="off">
  <h2>1. Wi-Fi</h2>
  <p class="daem" id="wifi-nu" hidden></p>
  <div class="raekke">
    <div><label for="ssid">Netværk</label>
      <input id="ssid" list="netvaerk" placeholder="Navn på dit Wi-Fi" maxlength="32"></div>
    <button type="button" class="sek" id="scan">Søg</button>
  </div>
  <datalist id="netvaerk"></datalist>
  <label for="wifi_kode">Wi-Fi-kode</label>
  <input id="wifi_kode" type="password" maxlength="64">

  <h2 style="margin-top:24px">2. Adgangskode til IHC Bridge</h2>
  <p class="daem">Bruges til at logge ind i web-UI'et og til nødbetjenings-access-point'et,
    hvis enheden mister Wi-Fi i mere end 10 minutter. Mindst 8 tegn.</p>
  <label for="kode">Adgangskode</label>
  <input id="kode" type="password" minlength="8" maxlength="63" required autocomplete="new-password">
  <label for="kode2">Gentag adgangskode</label>
  <input id="kode2" type="password" minlength="8" maxlength="63" required autocomplete="new-password">

  <p class="besked" id="besked"></p>
  <button type="submit" id="gem">Gem og forbind</button>
</form>

<div class="kort" id="faerdig" hidden>
  <h2>Opsætning gemt</h2>
  <p id="status">Forbinder til dit Wi-Fi …</p>
</div>

<script>
const $ = id => document.getElementById(id);
const besked = (t, klasse) => { $('besked').textContent = t; $('besked').className = 'besked ' + (klasse || ''); };

async function hent(url, opt) {
  const r = await fetch(url, opt);
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (!r.ok) throw new Error(j.fejl || ('Fejl ' + r.status));
  return j;
}

async function scan() {
  $('scan').disabled = true; besked('Søger efter netværk …');
  try {
    const j = await hent('/api/wifi/scan');
    const dl = $('netvaerk'); dl.textContent = '';
    j.netvaerk.sort((a, b) => b.rssi - a.rssi).forEach(n => {
      const o = document.createElement('option'); o.value = n.ssid; dl.appendChild(o);
    });
    besked(j.netvaerk.length + ' netværk fundet. Klik i feltet for at vælge.');
  } catch (e) { besked(e.message, 'fejl'); }
  $('scan').disabled = false;
}

async function init() {
  try {
    const j = await hent('/api/opsaetning');
    if (j.opsat) { location.href = '/'; return; }
    if (j.wifi.ssid) {
      $('wifi-nu').hidden = false;
      $('wifi-nu').textContent = 'Gemt netværk: ' + j.wifi.ssid + '. Lad felterne være tomme for at beholde det.';
    }
  } catch (e) {}
}

async function overvaag() {
  try {
    const j = await hent('/api/opsaetning');
    if (j.wifi.forbundet && j.wifi.ip) {
      const url = 'http://' + j.wifi.ip + '/';
      $('status').innerHTML = '';
      $('status').append('Enheden er på dit netværk med adressen ');
      const a = document.createElement('a'); a.href = url; a.textContent = url; $('status').append(a);
      $('status').append('. Skift tilbage til dit eget Wi-Fi og åbn adressen. Opsætnings-access-point\'et lukker om et minut.');
      return;
    }
    if (j.wifi.forsoeg > 2) $('status').textContent =
      'Forbinder stadig (forsøg ' + j.wifi.forsoeg + '). Er netværksnavn og kode rigtige? Genindlæs siden for at rette.';
  } catch (e) {}
  setTimeout(overvaag, 2000);
}

$('scan').onclick = scan;
$('form').onsubmit = async ev => {
  ev.preventDefault();
  if ($('kode').value !== $('kode2').value) { besked('De to adgangskoder er ikke ens', 'fejl'); return; }
  $('gem').disabled = true; besked('Gemmer …');
  try {
    await hent('/api/opsaetning', { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ kode: $('kode').value, wifi_ssid: $('ssid').value.trim(), wifi_kode: $('wifi_kode').value }) });
    $('form').hidden = true; $('faerdig').hidden = false;
    if (!$('ssid').value.trim()) { location.href = '/'; return; }
    overvaag();
  } catch (e) { besked(e.message, 'fejl'); $('gem').disabled = false; }
};
init();
</script>
</main></body></html>)HTML";

const char kIndex[] = R"HTML(<!doctype html>
<html lang="da"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IHC Bridge</title>)HTML" IHC_CSS R"HTML(<style>
.top{display:flex;justify-content:space-between;align-items:center}
.gitter{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:8px}
.kanal{border:1px solid var(--kant);border-radius:8px;padding:8px 10px;background:var(--bg);text-align:left;
  color:var(--tekst);font-size:.9rem;min-height:44px}
button.kanal{cursor:pointer}.kanal.til{background:var(--til);color:#1c1f24;border-color:var(--til)}
.modul{margin:0 0 16px}.modul h3{font-size:.95rem;margin:0 0 8px;color:var(--daem)}
.prik{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:var(--fejl)}
.prik.ok{background:var(--ok)}
</style></head><body><main>

<form id="login" class="kort" hidden style="max-width:360px;margin:15vh auto">
  <h1>IHC Bridge</h1>
  <label for="kode">Adgangskode</label>
  <input id="kode" type="password" autocomplete="current-password" required>
  <p class="besked" id="login-besked"></p>
  <button type="submit">Log ind</button>
</form>

<div id="app" hidden>
  <div class="top"><h1>IHC Bridge</h1><button class="sek" id="logud">Log ud</button></div>
  <div class="kort" id="status"></div>
  <div class="kort"><h2>Udgange</h2><div id="udgange"></div>
    <p class="daem" id="ingen-udgange" hidden>Ingen outputmoduler er slået til endnu. Det gøres i det fulde web-UI (fase 7)
      eller med <code>POST /api/moduler</code>.</p></div>
  <div class="kort"><h2>Indgange</h2><div id="indgange"></div>
    <p class="daem" id="ingen-indgange" hidden>Ingen inputmoduler fundet endnu. De vises automatisk, når de sender.</p></div>
  <p class="daem">Midlertidig oversigt. Det fulde web-UI kommer i fase 7.</p>
</div>

<script>
const $ = id => document.getElementById(id);
let io = null, ws = null;

async function hent(url, opt) {
  const r = await fetch(url, opt);
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (r.status === 401 && url !== '/api/login') visLogin();
  if (!r.ok) throw new Error(j.fejl || ('Fejl ' + r.status));
  return j;
}
const post = (url, data) => hent(url, { method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(data || {}) });

function visLogin() { $('app').hidden = true; $('login').hidden = false; $('kode').focus(); if (ws) ws.close(); }

function tegn() {
  const ud = $('udgange'); ud.textContent = '';
  const aktiveUd = io.udgange.filter(m => m.aktiv);
  $('ingen-udgange').hidden = aktiveUd.length > 0;
  aktiveUd.forEach(m => {
    const d = document.createElement('div'); d.className = 'modul';
    const h = document.createElement('h3'); h.textContent = 'Outputmodul ' + m.modul; d.appendChild(h);
    const g = document.createElement('div'); g.className = 'gitter';
    m.kanaler.forEach((k, i) => {
      const b = document.createElement('button'); b.className = 'kanal'; b.textContent = k.navn;
      b.dataset.m = m.modul; b.dataset.k = i + 1;
      b.onclick = () => skift(m.modul, i + 1);
      g.appendChild(b);
    });
    d.appendChild(g); ud.appendChild(d);
  });
  const ind = $('indgange'); ind.textContent = '';
  const aktiveInd = io.indgange.filter(m => m.aktiv);
  $('ingen-indgange').hidden = aktiveInd.length > 0;
  aktiveInd.forEach(m => {
    const d = document.createElement('div'); d.className = 'modul';
    const h = document.createElement('h3'); h.dataset.im = m.modul; d.appendChild(h);
    const g = document.createElement('div'); g.className = 'gitter';
    m.kanaler.forEach((k, i) => {
      const s = document.createElement('div'); s.className = 'kanal'; s.textContent = k.navn;
      s.dataset.im = m.modul; s.dataset.ik = i + 1; g.appendChild(s);
    });
    d.appendChild(g); ind.appendChild(d);
  });
  opdater();
}

function opdater() {
  document.querySelectorAll('button.kanal').forEach(b => {
    const m = io.udgange[b.dataset.m - 1];
    b.classList.toggle('til', !!(m.tilstand >> (b.dataset.k - 1) & 1));
  });
  document.querySelectorAll('h3[data-im]').forEach(h => {
    const m = io.indgange[h.dataset.im - 1];
    h.innerHTML = '<span class="prik' + (m.online ? ' ok' : '') + '"></span>';
    h.append('Inputmodul ' + m.modul + (m.online ? '' : ' (offline)'));
  });
  document.querySelectorAll('div.kanal[data-im]').forEach(s => {
    const m = io.indgange[s.dataset.im - 1];
    s.classList.toggle('til', m.online && !!(m.tilstand >> (s.dataset.ik - 1) & 1));
  });
}

function skift(m, k) {
  const besked = JSON.stringify({ type: 'udgang', modul: m, kanal: k, tilstand: 'skift' });
  if (ws && ws.readyState === 1) ws.send(besked);
  else post('/api/udgang', { modul: m, kanal: k, tilstand: 'skift' }).catch(e => alert(e.message));
}

function forbindWs() {
  ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
  ws.onmessage = ev => {
    const d = JSON.parse(ev.data);
    if (d.type !== 'tilstand' || !io) return;
    for (const m in (d.udgange || {})) io.udgange[m - 1].tilstand = d.udgange[m];
    let nyeModuler = false;
    for (const m in (d.indgange || {})) {
      const mod = io.indgange[m - 1];
      if (d.indgange[m].online && !mod.aktiv) { mod.aktiv = true; nyeModuler = true; }
      mod.online = d.indgange[m].online; mod.tilstand = d.indgange[m].tilstand;
    }
    nyeModuler ? tegn() : opdater();
  };
  ws.onclose = () => { if (!$('app').hidden) setTimeout(forbindWs, 2000); };
}

async function visStatus() {
  try {
    const s = await hent('/api/status');
    const linjer = [
      ['Wi-Fi', s.wifi.forbundet ? s.wifi.ssid + ' (' + s.wifi.ip + ', ' + s.wifi.rssi + ' dBm)' : 'ikke forbundet'],
      ['MQTT', !s.mqtt.aktiveret ? 'ikke sat op' : s.mqtt.forbundet ? 'forbundet' : 'ikke forbundet ' + s.mqtt.fejl],
      ['Firmware', s.system.version + (s.system.firmware_afventer_bekraeftelse ? ' (ny, afventer bekræftelse)' : '')],
      ['Oppetid', Math.floor(s.system.oppetid_s / 3600) + ' t ' + Math.floor(s.system.oppetid_s % 3600 / 60) + ' min'],
    ];
    const el = $('status'); el.textContent = '';
    linjer.forEach(([k, v]) => { const p = document.createElement('div'); p.textContent = k + ': ' + v; el.appendChild(p); });
  } catch (e) {}
}

async function start() {
  try {
    io = await hent('/api/io');
    $('login').hidden = true; $('app').hidden = false;
    tegn(); forbindWs(); visStatus(); setInterval(visStatus, 10000);
  } catch (e) {}
}

$('login').onsubmit = async ev => {
  ev.preventDefault();
  try {
    await post('/api/login', { kode: $('kode').value });
    $('kode').value = ''; $('login-besked').textContent = ''; start();
  } catch (e) { $('login-besked').textContent = e.message; $('login-besked').className = 'besked fejl'; }
};
$('logud').onclick = async () => { await post('/api/logud').catch(() => {}); visLogin(); };
start();
</script>
</main></body></html>)HTML";

}  // namespace web_sider

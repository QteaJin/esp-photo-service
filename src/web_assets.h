#pragma once
#include <pgmspace.h>

// Веб-панель: три вкладки (Camera, Updates, Diagnostics). Без CDN, работает в изолированной сети.
// Тёмная и светлая темы переключаются сами по настройке системы.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Cam Panel</title>
<style>
  :root {
    --bg: #f3f5f7; --surface: #ffffff; --surface-2: #eaeef2; --border: #d3dae1;
    --fg: #17212b; --muted: #5d6b79; --accent: #0b6bcb; --accent-fg: #ffffff;
    --good: #1a7f4b; --warn: #a75a00; --bad: #c62d3a;
    --good-bg: #dff3e8; --warn-bg: #fdecd2; --bad-bg: #fbe0e2;
    --mono: ui-monospace, "Cascadia Mono", Consolas, monospace;
    --sans: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    color-scheme: light;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #11161c; --surface: #19212a; --surface-2: #222c37; --border: #2f3b48;
      --fg: #e6ebf0; --muted: #94a3b2; --accent: #5aa7ff; --accent-fg: #07131f;
      --good: #4cc98a; --warn: #f0a94a; --bad: #ff7a85;
      --good-bg: #14301f; --warn-bg: #3a2a10; --bad-bg: #3b1a1e; color-scheme: dark;
    }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--fg); font-family: var(--sans); font-size: 15px; line-height: 1.45; padding: 0 16px 2rem; }
  .wrap { max-width: 880px; margin: 0 auto; }
  button, input { font: inherit; color: inherit; }
  button:focus-visible, input:focus-visible, a:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  [hidden] { display: none !important; }

  header { position: sticky; top: 0; z-index: 5; background: var(--bg); padding-block: .9rem .1rem; }
  .top { display: flex; align-items: baseline; justify-content: space-between; gap: .75rem; flex-wrap: wrap; }
  h1 { font-size: 1.15rem; margin: 0; font-weight: 650; }
  .ver { font-family: var(--mono); font-size: .8rem; color: var(--muted); }
  .chips { display: flex; flex-wrap: wrap; gap: .4rem; margin-top: .55rem; }
  .chip { display: inline-flex; align-items: center; gap: .4rem; padding: .22rem .6rem; border-radius: 999px; background: var(--surface-2); font-size: .82rem; white-space: nowrap; }
  .chip i { width: .55rem; height: .55rem; border-radius: 50%; background: var(--muted); display: inline-block; }
  .chip.good i { background: var(--good); } .chip.warn i { background: var(--warn); } .chip.bad i { background: var(--bad); }
  .chip b { font-weight: 650; font-variant-numeric: tabular-nums; }

  .tabs { display: flex; gap: .25rem; border-bottom: 1px solid var(--border); margin-top: .7rem; overflow-x: auto; }
  [role=tab] { background: none; border: 0; border-bottom: 3px solid transparent; padding: .6rem .9rem; cursor: pointer; color: var(--muted); font-weight: 600; white-space: nowrap; display: inline-flex; gap: .45rem; align-items: center; min-height: 44px; }
  [role=tab][aria-selected=true] { color: var(--fg); border-bottom-color: var(--accent); }
  .badge { background: var(--warn-bg); color: var(--warn); border-radius: 999px; font-size: .72rem; padding: .05rem .45rem; font-weight: 700; }
  .badge.bad { background: var(--bad-bg); color: var(--bad); }
  section[role=tabpanel] { padding-top: 1rem; display: grid; gap: 1rem; }

  .card { background: var(--surface); border: 1px solid var(--border); border-radius: 10px; padding: 1rem; min-width: 0; }
  .card h2 { font-size: .8rem; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); margin: 0 0 .75rem; font-weight: 700; }
  .cols { display: grid; gap: 1rem; grid-template-columns: 1fr; }
  .stack { display: grid; gap: 1rem; min-width: 0; }
  @media (min-width: 760px) { .cols.two { grid-template-columns: 1.15fr 1fr; align-items: start; } }

  .btn { background: var(--surface-2); border: 1px solid var(--border); border-radius: 8px; padding: .5rem .9rem; cursor: pointer; min-height: 40px; font-weight: 600; text-decoration: none; display: inline-flex; align-items: center; }
  .btn:hover:not(:disabled) { border-color: var(--muted); }
  .btn.primary { background: var(--accent); color: var(--accent-fg); border-color: transparent; }
  .btn.danger { background: var(--bad); color: #fff; border-color: transparent; }
  .btn.go { background: var(--good); color: #fff; border-color: transparent; }
  .btn:disabled { opacity: .45; cursor: default; }
  .actions { display: flex; flex-wrap: wrap; gap: .5rem; align-items: center; }
  .field { display: grid; grid-template-columns: 7.5rem 1fr; align-items: center; gap: .5rem; margin-block: .5rem; }
  .field input { width: 100%; min-width: 0; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; padding: .45rem .6rem; }
  .hint { color: var(--muted); font-size: .84rem; margin: .35rem 0 0; }
  .msg { min-height: 1.3rem; font-size: .88rem; margin: .5rem 0 0; overflow-wrap: anywhere; }
  .msg.ok { color: var(--good); } .msg.err { color: var(--bad); } .msg.warn { color: var(--warn); }
  .mono { font-family: var(--mono); font-size: .82rem; overflow-wrap: anywhere; }

  .preview { aspect-ratio: 4 / 3; max-width: 100%; border-radius: 8px; overflow: hidden; position: relative; border: 1px solid var(--border); background: var(--surface-2); }
  .preview img { width: 100%; height: 100%; object-fit: contain; display: block; }
  .preview .tag { position: absolute; left: .6rem; bottom: .6rem; background: rgba(0,0,0,.6); color: #fff; font-family: var(--mono); font-size: .75rem; padding: .15rem .5rem; border-radius: 6px; }

  .crow { display: grid; grid-template-columns: 1fr auto 5.5rem auto; align-items: center; gap: .4rem; padding-block: .25rem; }
  .crow .step { width: 44px; height: 40px; padding: 0; font-size: 1.2rem; justify-content: center; }
  .crow .val { text-align: center; font-weight: 650; font-variant-numeric: tabular-nums; }
  .crow.changed .val { color: var(--warn); }
  .dirty { background: var(--warn-bg); color: var(--warn); border-radius: 8px; padding: .4rem .7rem; font-size: .86rem; margin-bottom: .6rem; }

  dl.kv { margin: 0; display: grid; grid-template-columns: auto 1fr; gap: .35rem 1rem; }
  dl.kv dt { color: var(--muted); } dl.kv dd { margin: 0; font-weight: 650; text-align: right; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
  .grid-kv { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: .6rem; }
  .stat { background: var(--surface-2); border-radius: 8px; padding: .55rem .7rem; min-width: 0; }
  .stat small { color: var(--muted); display: block; font-size: .76rem; }
  .stat b { font-size: 1.08rem; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
  .stat.good b { color: var(--good); } .stat.warn b { color: var(--warn); } .stat.bad b { color: var(--bad); }
  .bar { height: 8px; border-radius: 99px; background: var(--surface-2); overflow: hidden; margin-top: .4rem; }
  .bar > span { display: block; height: 100%; background: var(--accent); width: 0; transition: width .2s; }

  .result { border: 1px solid var(--border); border-radius: 8px; padding: .8rem; margin-top: .75rem; background: var(--bg); }
  .result.new { border-color: var(--good); }
  .confirm { margin-top: .7rem; background: var(--warn-bg); border-radius: 8px; padding: .7rem; }

  .toolbar { display: flex; flex-wrap: wrap; gap: .5rem; align-items: center; justify-content: space-between; margin-bottom: .6rem; }
  .toolbar h2 { margin: 0; }
  .seg { display: inline-flex; border: 1px solid var(--border); border-radius: 8px; overflow: hidden; }
  .seg button { background: var(--surface); border: 0; padding: .4rem .75rem; cursor: pointer; min-height: 38px; color: var(--muted); font-weight: 600; }
  .seg button[aria-pressed=true] { background: var(--accent); color: var(--accent-fg); }
  .log { background: #0e1318; color: #d4dde6; font-family: var(--mono); font-size: .78rem; line-height: 1.5; border-radius: 8px; padding: .6rem .7rem; max-height: 340px; overflow: auto; margin: 0; }
  .log div { white-space: pre-wrap; overflow-wrap: anywhere; }
  .log .t { color: #7d8b99; } .log .W { color: #f0b35a; } .log .E { color: #ff8b95; } .log .sep { color: #7d8b99; border-bottom: 1px dashed #3a4856; margin-bottom: .4rem; padding-bottom: .4rem; }
  .toggle { display: inline-flex; align-items: center; gap: .5rem; cursor: pointer; user-select: none; }
  .toggle input { width: 1.1rem; height: 1.1rem; accent-color: var(--accent); }
  .alerts { display: grid; gap: .5rem; }
  .alert { border-radius: 8px; padding: .55rem .75rem; font-size: .9rem; display: flex; gap: .6rem; justify-content: space-between; flex-wrap: wrap; }
  .alert.warn { background: var(--warn-bg); } .alert.bad { background: var(--bad-bg); } .alert.good { background: var(--good-bg); }
  .alert time { font-family: var(--mono); color: var(--muted); font-size: .78rem; }
  @media (max-width: 480px) { .field { grid-template-columns: 1fr; gap: .2rem; } .crow { grid-template-columns: 1fr auto 4.2rem auto; } }
  @media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="top">
      <h1 id="title">ESP32 Camera</h1>
      <span class="ver" id="hdr-ver"></span>
    </div>
    <div class="chips" id="chips"></div>
    <div class="tabs" role="tablist" aria-label="Sections">
      <button role="tab" id="t-camera" aria-controls="p-camera" aria-selected="true">Camera</button>
      <button role="tab" id="t-updates" aria-controls="p-updates" aria-selected="false">Updates</button>
      <button role="tab" id="t-diag" aria-controls="p-diag" aria-selected="false">Diagnostics <span class="badge" id="diag-badge" hidden>0</span></button>
    </div>
  </header>

  <section role="tabpanel" id="p-camera" aria-labelledby="t-camera">
    <div class="cols two">
      <div class="card">
        <h2>Live preview</h2>
        <div class="preview"><img id="preview-img" alt="Camera preview"><span class="tag" id="prev-tag">640 x 480, rotated 180&deg;</span></div>
        <div class="actions" style="margin-top:.75rem"><button class="btn primary" id="btn-prev">Refresh preview</button></div>
      </div>
      <div class="stack">
        <div class="card">
          <h2>Auto capture</h2>
          <div class="field"><label for="interval">Interval, sec</label><input type="number" id="interval" value="30" min="1"></div>
          <div class="actions">
            <button class="btn go" id="btn-start">Start</button>
            <button class="btn danger" id="btn-stop" disabled>Stop</button>
          </div>
          <p class="msg" id="auto-msg"></p>
        </div>
        <div class="card">
          <h2>Photo server</h2>
          <div class="field"><label for="srv-ip">Server IP</label><input type="text" id="srv-ip" inputmode="decimal" placeholder="192.168.0.212" autocomplete="off"></div>
          <div class="field"><label for="srv-port">Port</label><input type="number" id="srv-port" value="8000" min="1" max="65535"></div>
          <p class="hint" id="srv-hint">Photos go to http://IP:PORT/upload</p>
          <div class="actions" style="margin-top:.5rem"><button class="btn" id="btn-srv">Save server</button></div>
          <p class="msg" id="srv-msg"></p>
        </div>
      </div>
    </div>
    <div class="card">
      <h2>Colour tuning</h2>
      <div class="dirty" id="dirty" hidden>Unsaved changes. They are lost on restart.</div>
      <div id="color-rows"></div>
      <div class="actions" style="margin-top:.7rem">
        <button class="btn primary" id="btn-save" disabled>Save</button>
        <button class="btn" id="btn-reset">Reset to defaults</button>
      </div>
      <p class="msg" id="color-msg"></p>
    </div>
  </section>

  <section role="tabpanel" id="p-updates" aria-labelledby="t-updates" hidden>
    <div class="cols two">
      <div class="card">
        <h2>Firmware</h2>
        <dl class="kv">
          <dt>Installed version</dt><dd class="mono" id="fw-ver">-</dd>
          <dt>Built</dt><dd id="fw-built">-</dd>
          <dt>Running from</dt><dd id="fw-slot">-</dd>
          <dt>Size</dt><dd id="fw-size">-</dd>
        </dl>
        <div class="actions" style="margin-top:1rem"><button class="btn primary" id="btn-check">Check for update</button></div>
        <div id="upd-result"></div>
      </div>
      <div class="stack">
        <div class="card">
          <h2>Firmware server</h2>
          <div class="field"><label for="fw-ip">Server IP</label><input type="text" id="fw-ip" inputmode="decimal" placeholder="192.168.0.220" autocomplete="off"></div>
          <div class="field"><label for="fw-port">Port</label><input type="number" id="fw-port" value="8080" min="1" max="65535"></div>
          <p class="hint">Reads <span class="mono">/version.json</span> and <span class="mono">/firmware.bin</span> from this server.</p>
          <div class="actions" style="margin-top:.5rem"><button class="btn" id="btn-fw-save">Save firmware server</button></div>
          <p class="msg" id="fw-msg"></p>
        </div>
        <div class="card">
          <h2>Upload from computer</h2>
          <dl class="kv">
            <dt>Wi-Fi upload</dt><dd style="color:var(--good)">enabled</dd>
            <dt>Host name</dt><dd class="mono" id="ota-host">-</dd>
          </dl>
          <p class="hint">From the project folder: <span class="mono">pio run -e esp32-s3-cam-ota -t upload</span></p>
        </div>
      </div>
    </div>
  </section>

  <section role="tabpanel" id="p-diag" aria-labelledby="t-diag" hidden>
    <div class="card">
      <h2>Warnings</h2>
      <div class="alerts" id="alerts"></div>
    </div>
    <div class="card">
      <div class="toolbar">
        <h2>Board health</h2>
        <div class="actions">
          <label class="toggle"><input type="checkbox" id="auto-ref"> Auto-refresh every 15 s</label>
          <button class="btn" id="btn-health">Refresh now</button>
        </div>
      </div>
      <div class="grid-kv" id="health"></div>
      <p class="hint" id="health-note"></p>
    </div>
    <div class="card">
      <div class="toolbar">
        <h2>Event log</h2>
        <div class="actions">
          <div class="seg" role="group" aria-label="Which run">
            <button id="src-now" aria-pressed="true">This boot</button>
            <button id="src-prev" aria-pressed="false">Before last restart</button>
          </div>
          <div class="seg" role="group" aria-label="Level">
            <button data-lvl="all" aria-pressed="true">All</button>
            <button data-lvl="W" aria-pressed="false">Warnings</button>
            <button data-lvl="E" aria-pressed="false">Errors</button>
          </div>
        </div>
      </div>
      <pre class="log" id="log" tabindex="0"></pre>
      <div class="actions" style="margin-top:.6rem">
        <button class="btn" id="btn-log">Reload log</button>
        <button class="btn" id="btn-copy">Copy to clipboard</button>
        <a class="btn" href="/log" target="_blank" rel="noopener">Open raw /log</a>
      </div>
      <p class="hint">Kept in memory only: the last 50 lines. The copy from before a restart survives a crash or watchdog reset, but not a power cut.</p>
    </div>
  </section>
</div>
<script src="/main.js"></script>
</body>
</html>)HTML";

static const char MAIN_JS[] PROGMEM = R"JS((function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var ESC = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" };
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return ESC[c]; }); }
  function nowText() { return new Date().toLocaleTimeString(); }
  function errText(e) { return e && e.message ? e.message : String(e); }
  function checkOk(r) {
    if (r.ok) return r;
    return r.text().then(function (t) { throw new Error(t || ("HTTP " + r.status)); });
  }
  function getJson(url) { return fetch(url, { cache: "no-store" }).then(checkOk).then(function (r) { return r.json(); }); }
  function getText(url) { return fetch(url, { cache: "no-store" }).then(checkOk).then(function (r) { return r.text(); }); }
  function setMsg(id, text, cls) { var el = $(id); el.textContent = text; el.className = "msg" + (cls ? " " + cls : ""); }
  function visible() { return document.visibilityState === "visible"; }
  function fmtUp(s) {
    var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return (d ? d + " d " : "") + h + " h " + m + " min";
  }
  function fmtAgo(s) { return s < 60 ? s + " s" : (s < 3600 ? Math.floor(s / 60) + " min" : Math.floor(s / 3600) + " h"); }
  function fmtKB(b) { return Math.round(b / 1024) + " KB"; }

  var H = null;               // последний ответ /health.json
  var autoRunning = false, curInterval = 30;
  var tabNames = ["camera", "updates", "diag"];
  var currentTab = "camera";

  /* ---- Вкладки (адрес #camera, #updates, #diag) ---- */
  function showTab(name) {
    if (tabNames.indexOf(name) < 0) name = "camera";
    currentTab = name;
    tabNames.forEach(function (n) {
      $("t-" + n).setAttribute("aria-selected", n === name ? "true" : "false");
      $("p-" + n).hidden = n !== name;
    });
    if (name === "diag") { loadHealth(); loadLog(); }
    if (name === "updates") loadFwInfo();
  }
  tabNames.forEach(function (n) {
    $("t-" + n).addEventListener("click", function () {
      showTab(n);
      try { history.replaceState(null, "", "#" + n); } catch (e) {}
    });
  });
  window.addEventListener("hashchange", function () { showTab(location.hash.slice(1)); });

  /* ---- Шапка со статусом ---- */
  function tempCls(t) { return t >= 80 ? "bad" : (t >= 70 ? "warn" : "good"); }
  function rssiCls(r) { return r >= -65 ? "good" : (r >= -75 ? "warn" : "bad"); }
  function chip(cls, label, val) { return '<span class="chip ' + cls + '"><i></i>' + label + " <b>" + esc(val) + "</b></span>"; }
  function renderChips() {
    var html = "";
    if (H) {
      html += chip(tempCls(H.temp), "Temp", H.temp.toFixed(1) + " °C");
      html += chip(rssiCls(H.rssi), "Wi-Fi", H.rssi + " dBm");
    }
    html += chip(autoRunning ? "good" : "", "Auto capture", autoRunning ? "running" : "stopped");
    html += chip("", "IP", H ? H.ip : location.hostname);
    $("chips").innerHTML = html;
  }

  /* ---- Состояние платы ---- */
  function stat(cls, label, val) { return '<div class="stat ' + cls + '"><small>' + label + "</small><b>" + esc(val) + "</b></div>"; }
  function renderHealth() {
    if (!H) return;
    $("health").innerHTML =
      stat(tempCls(H.temp), "Chip temperature", H.temp.toFixed(1) + " °C") +
      stat(rssiCls(H.rssi), "Wi-Fi signal", H.rssi + " dBm") +
      stat("", "Uptime", fmtUp(H.uptime)) +
      stat("", "Free heap", fmtKB(H.heap) + " / " + fmtKB(H.heap_total)) +
      stat("", "Free PSRAM", fmtKB(H.psram) + " / " + fmtKB(H.psram_total)) +
      stat(H.up_fail ? "warn" : "", "Photos sent", H.up_ok + (H.up_fail ? " (" + H.up_fail + " failed)" : "")) +
      stat("", "Average rate", H.up_rate.toFixed(1) + " per min") +
      stat(H.reset_bad ? "bad" : "", "Last restart", H.reset);
  }
  function loadHealth() {
    return getJson("/health.json").then(function (d) {
      H = d;
      renderChips(); renderHealth(); renderAuto();
      $("health-note").textContent = "Updated " + nowText() + ".";
    }).catch(function (e) {
      $("health-note").textContent = "Failed to read board status: " + errText(e);
    });
  }

  /* ---- Автосъёмка ---- */
  function renderAuto() {
    $("btn-start").disabled = autoRunning;
    $("btn-stop").disabled = !autoRunning;
    $("btn-srv").disabled = autoRunning;
    $("srv-hint").textContent = autoRunning ? "Stop auto capture to change the server." : "Photos go to http://IP:PORT/upload";
    var t = autoRunning ? "Running, one photo every " + curInterval + " s." : "Stopped.";
    if (H && H.up_ago >= 0) t += " Last upload: HTTP " + H.up_code + ", " + fmtAgo(H.up_ago) + " ago.";
    setMsg("auto-msg", t);
  }
  function loadStatus() {
    return getJson("/status").then(function (d) {
      autoRunning = d.running; curInterval = d.interval;
      renderAuto(); renderChips();
    }).catch(function (e) { setMsg("auto-msg", "Failed to get status: " + errText(e), "err"); });
  }
  $("btn-start").addEventListener("click", function () {
    var interval = Math.max(1, parseInt($("interval").value || "30", 10));
    setMsg("auto-msg", "Starting...");
    getText("/auto_start?interval=" + interval).then(loadStatus)
      .catch(function (e) { setMsg("auto-msg", "Failed to start: " + errText(e), "err"); });
  });
  $("btn-stop").addEventListener("click", function () {
    setMsg("auto-msg", "Stopping...");
    getText("/auto_stop").then(loadStatus)
      .catch(function (e) { setMsg("auto-msg", "Failed to stop: " + errText(e), "err"); });
  });

  /* ---- Превью ---- */
  var previewTimer = null;
  function refreshPreview() { $("preview-img").src = "/preview?ts=" + Date.now(); }
  $("preview-img").addEventListener("load", function () { $("prev-tag").textContent = "640 x 480, rotated 180°, " + nowText(); });
  $("preview-img").addEventListener("error", function () { $("prev-tag").textContent = "Camera error"; });
  $("btn-prev").addEventListener("click", refreshPreview);
  function schedulePreview() { clearTimeout(previewTimer); previewTimer = setTimeout(refreshPreview, 700); }

  /* ---- Цвет ---- */
  var WB = ["Auto", "Sunny", "Cloudy", "Office", "Home"];
  var PARAMS = [
    { k: "brightness", l: "Brightness", min: -2, max: 2 }, { k: "contrast", l: "Contrast", min: -2, max: 2 },
    { k: "saturation", l: "Saturation", min: -2, max: 2 }, { k: "ae_level", l: "Exposure", min: -2, max: 2 },
    { k: "wb_mode", l: "White balance", min: 0, max: 4 }, { k: "lenc", l: "Lens correction", min: 0, max: 1 }
  ];
  var saved = null, live = null;
  function fmtColor(k, v) { return k === "wb_mode" ? (WB[v] || v) : (k === "lenc" ? (v ? "On" : "Off") : (v > 0 ? "+" + v : String(v))); }
  function renderColor() {
    if (!live) return;
    var dirty = false, html = "";
    PARAMS.forEach(function (p) {
      var ch = saved && live[p.k] !== saved[p.k];
      if (ch) dirty = true;
      html += '<div class="crow' + (ch ? " changed" : "") + '"><span>' + p.l + "</span>" +
        '<button class="btn step" data-k="' + p.k + '" data-d="-1" aria-label="Decrease ' + p.l + '">−</button>' +
        '<span class="val">' + fmtColor(p.k, live[p.k]) + "</span>" +
        '<button class="btn step" data-k="' + p.k + '" data-d="1" aria-label="Increase ' + p.l + '">+</button></div>';
    });
    $("color-rows").innerHTML = html;
    $("dirty").hidden = !dirty;
    $("btn-save").disabled = !dirty;
  }
  $("color-rows").addEventListener("click", function (e) {
    var b = e.target.closest("button[data-k]");
    if (!b || !live) return;
    var p = PARAMS.filter(function (x) { return x.k === b.dataset.k; })[0];
    var next = Math.min(p.max, Math.max(p.min, live[p.k] + parseInt(b.dataset.d, 10)));
    if (next === live[p.k]) return;
    getJson("/control?var=" + p.k + "&val=" + next).then(function (d) {
      live = d; renderColor();
      setMsg("color-msg", "Applied at " + nowText() + ". Press Save to keep it after a restart.", "warn");
      schedulePreview();
    }).catch(function (err) { setMsg("color-msg", "Colour change failed: " + errText(err), "err"); });
  });
  $("btn-save").addEventListener("click", function () {
    getText("/camera_save").then(function () { return loadSettings(false); }).then(function () {
      setMsg("color-msg", "Saved and confirmed on the board at " + nowText() + ".", "ok");
    }).catch(function (e) { setMsg("color-msg", "Save failed: " + errText(e), "err"); });
  });
  $("btn-reset").addEventListener("click", function () {
    getJson("/camera_reset").then(function (d) {
      live = d; renderColor();
      setMsg("color-msg", "Reset to defaults at " + nowText() + ". Press Save to keep it.", "warn");
      schedulePreview();
    }).catch(function (e) { setMsg("color-msg", "Reset failed: " + errText(e), "err"); });
  });

  /* ---- Сохранённые настройки и сервер фото ---- */
  function loadSettings(fill) {
    return getJson("/settings").then(function (s) {
      saved = s.saved; live = s.live;
      autoRunning = s.running; curInterval = s.interval;
      if (fill) {
        $("srv-ip").value = s.server.ip; $("srv-port").value = s.server.port; $("interval").value = s.interval;
      }
      renderColor(); renderAuto(); renderChips();
    }).catch(function (e) { setMsg("color-msg", "Failed to read settings: " + errText(e), "err"); throw e; });
  }
  $("btn-srv").addEventListener("click", function () {
    setMsg("srv-msg", "Saving...");
    getJson("/set_server?ip=" + encodeURIComponent($("srv-ip").value.trim()) + "&port=" + encodeURIComponent($("srv-port").value.trim() || "8000"))
      .then(function (s) { setMsg("srv-msg", "Saved and confirmed at " + nowText() + ": " + s.url, "ok"); })
      .catch(function (e) { setMsg("srv-msg", "Save failed: " + errText(e), "err"); });
  });

  /* ---- Обновления ---- */
  var fwLoaded = false;
  function loadFwInfo() {
    return getJson("/fw_info").then(function (d) {
      $("hdr-ver").textContent = "firmware " + d.version;
      $("title").textContent = d.host;
      $("fw-ver").textContent = d.version; $("fw-built").textContent = d.built;
      $("fw-slot").textContent = d.slot; $("fw-size").textContent = (d.size / 1048576).toFixed(2) + " MB";
      $("ota-host").textContent = d.host + ".local";
      if (!fwLoaded) { $("fw-ip").value = d.fw_ip; $("fw-port").value = d.fw_port; fwLoaded = true; }
    }).catch(function (e) { setMsg("fw-msg", "Failed to read firmware info: " + errText(e), "err"); });
  }
  $("btn-fw-save").addEventListener("click", function () {
    setMsg("fw-msg", "Saving...");
    getJson("/fw_server_set?ip=" + encodeURIComponent($("fw-ip").value.trim()) + "&port=" + encodeURIComponent($("fw-port").value.trim()))
      .then(function (d) { setMsg("fw-msg", "Saved and confirmed at " + nowText() + ": " + d.fw_ip + ":" + d.fw_port, "ok"); })
      .catch(function (e) { setMsg("fw-msg", "Save failed: " + errText(e), "err"); });
  });
  $("btn-check").addEventListener("click", function () {
    var box = $("upd-result");
    box.innerHTML = '<div class="result">Asking the firmware server...</div>';
    $("btn-check").disabled = true;
    getJson("/fw_check").then(function (d) {
      if (!d.ok) { box.innerHTML = '<div class="result"><span style="color:var(--bad)">' + esc(d.error) + "</span></div>"; return; }
      if (!d.newer) {
        box.innerHTML = '<div class="result">You are up to date. The server has the same version: <span class="mono">' + esc(d.available) + "</span></div>";
        return;
      }
      box.innerHTML = '<div class="result new"><b>Update available</b><dl class="kv" style="margin-top:.5rem">' +
        '<dt>Installed</dt><dd class="mono">' + esc(d.installed) + '</dd><dt>On server</dt><dd class="mono">' + esc(d.available) + "</dd>" +
        "<dt>Size</dt><dd>" + (d.size / 1048576).toFixed(2) + ' MB</dd><dt>SHA-256</dt><dd class="mono">' + esc(d.sha256.slice(0, 8)) + "&hellip;" + esc(d.sha256.slice(-8)) + "</dd></dl>" +
        '<div class="actions" style="margin-top:.8rem"><button class="btn primary" id="btn-install">Install update</button></div><div id="confirm"></div></div>';
      $("btn-install").addEventListener("click", askConfirm);
    }).catch(function (e) {
      box.innerHTML = '<div class="result"><span style="color:var(--bad)">Check failed: ' + esc(errText(e)) + "</span></div>";
    }).then(function () { $("btn-check").disabled = false; });
  });
  function askConfirm() {
    $("confirm").innerHTML = '<div class="confirm">The board will stop auto capture, download the file, verify it and restart. This takes about 30 seconds.' +
      '<div class="actions" style="margin-top:.6rem"><button class="btn primary" id="btn-yes">Yes, install</button><button class="btn" id="btn-no">Cancel</button></div></div>';
    $("btn-no").addEventListener("click", function () { $("confirm").innerHTML = ""; });
    $("btn-yes").addEventListener("click", runInstall);
  }
  function runInstall() {
    $("confirm").innerHTML = '<div class="confirm"><span id="inst-txt">Starting...</span><div class="bar"><span id="inst-bar"></span></div></div>';
    $("btn-install").disabled = true; $("btn-check").disabled = true;
    getText("/fw_install").then(pollInstall).catch(function (e) {
      $("inst-txt").textContent = "Could not start: " + errText(e);
    });
  }
  function pollInstall() {
    var seen = false, fails = 0;
    var t = setInterval(function () {
      getJson("/fw_status").then(function (s) {
        fails = 0;
        if (s.state === "running" || s.state === "done") {
          seen = true;
          $("inst-txt").textContent = s.message + (s.state === "running" ? "... " + s.percent + "%" : "...");
          $("inst-bar").style.width = s.percent + "%";
        } else if (s.state === "error") {
          clearInterval(t);
          $("inst-txt").textContent = "Update failed: " + s.message + ". The board keeps the old firmware.";
          $("btn-check").disabled = false;
        } else if (seen) {   // плата перезагрузилась и снова отвечает: обновление применено
          clearInterval(t);
          location.reload();
        }
      }).catch(function () {
        fails++;
        if (seen && fails >= 2) $("inst-txt").textContent = "Restarting, waiting for the board...";
      });
    }, 1000);
  }

  /* ---- Журнал ---- */
  var logSrc = "now", logLvl = "all", logData = { now: [], prev: [] };
  function parseLog(text) {
    return text.split("\n").filter(Boolean).map(function (l) {
      var m = /^(\S+) ([IWE]) (.*)$/.exec(l);
      return m ? { t: m[1], l: m[2], m: m[3] } : { t: "", l: "I", m: l };
    });
  }
  function loadLog() {
    return Promise.all([getText("/log?src=now"), getText("/log?src=prev")]).then(function (r) {
      logData.now = parseLog(r[0]); logData.prev = parseLog(r[1]);
      renderLog(); renderAlerts();
    }).catch(function (e) { $("log").textContent = "Failed to read the log: " + errText(e); });
  }
  function renderLog() {
    var rows = logData[logSrc].filter(function (r) { return logLvl === "all" || r.l === logLvl; });
    var head = "";
    if (logSrc === "prev") {
      head = '<div class="sep">' + (logData.prev.length ? "Previous run, ended by: " + esc(H ? H.reset : "?") : "Nothing kept: the board was powered on.") + "</div>";
    }
    $("log").innerHTML = head + (rows.length ? rows.map(function (r) {
      return '<div class="' + r.l + '"><span class="t">' + esc(r.t) + "</span> " + r.l + " " + esc(r.m) + "</div>";
    }).join("") : '<div class="t">No lines at this level.</div>');
    $("log").scrollTop = $("log").scrollHeight;
  }
  function renderAlerts() {
    var bad = logData.now.filter(function (r) { return r.l !== "I"; });
    var html = "";
    if (H && H.reset_bad) html += '<div class="alert bad"><span>The last restart was abnormal: ' + esc(H.reset) + '. See "Before last restart" below.</span></div>';
    bad.slice(-5).reverse().forEach(function (r) {
      html += '<div class="alert ' + (r.l === "E" ? "bad" : "warn") + '"><span>' + esc(r.m) + "</span><time>" + esc(r.t) + "</time></div>";
    });
    $("alerts").innerHTML = html || '<div class="alert good">No warnings since the last restart.</div>';
    var n = bad.length + (H && H.reset_bad ? 1 : 0);
    var badge = $("diag-badge");
    badge.textContent = n; badge.hidden = n === 0;
    badge.className = "badge" + ((H && H.reset_bad) || bad.some(function (r) { return r.l === "E"; }) ? " bad" : "");
  }
  function setPressed(group, el) {
    group.forEach(function (b) { b.setAttribute("aria-pressed", b === el ? "true" : "false"); });
  }
  var srcBtns = [$("src-now"), $("src-prev")];
  $("src-now").addEventListener("click", function () { logSrc = "now"; setPressed(srcBtns, this); renderLog(); });
  $("src-prev").addEventListener("click", function () { logSrc = "prev"; setPressed(srcBtns, this); renderLog(); });
  var lvlBtns = Array.prototype.slice.call(document.querySelectorAll("[data-lvl]"));
  lvlBtns.forEach(function (b) {
    b.addEventListener("click", function () { logLvl = b.dataset.lvl; setPressed(lvlBtns, b); renderLog(); });
  });
  $("btn-log").addEventListener("click", loadLog);
  $("btn-copy").addEventListener("click", function () {
    var done = function () { $("btn-copy").textContent = "Copied"; setTimeout(function () { $("btn-copy").textContent = "Copy to clipboard"; }, 1500); };
    try { navigator.clipboard.writeText($("log").innerText).then(done, function () {}); } catch (e) {}
  });

  /* ---- Автообновление состояния раз в 15 с (по умолчанию выключено) ---- */
  var refTimer = null;
  $("auto-ref").addEventListener("change", function () {
    clearInterval(refTimer);
    if ($("auto-ref").checked) {
      refTimer = setInterval(function () {
        if (!visible()) return;
        loadHealth();
        if (currentTab === "diag") loadLog();
      }, 15000);
      $("health-note").textContent = "Auto-refresh is on. Updates every 15 s while this page is open.";
    } else {
      $("health-note").textContent = "Auto-refresh is off.";
    }
  });
  $("btn-health").addEventListener("click", function () { loadHealth(); loadLog(); });

  /* Состояние автосъёмки проверяем раз в 5 с, пока страница видна */
  setInterval(function () { if (visible()) loadStatus(); }, 5000);
  document.addEventListener("visibilitychange", function () { if (visible()) { loadStatus(); loadHealth(); } });

  showTab(location.hash.slice(1));
  renderChips();
  loadFwInfo();
  loadSettings(true).catch(function () {}).then(function () { return loadHealth(); }).then(loadLog).then(refreshPreview);
})();
)JS";

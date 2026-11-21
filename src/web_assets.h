#pragma once
#include <pgmspace.h>

// Страница панели (аналог templates/index.html). Без CDN - работает в изолированной сети.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Camera</title>
  <style>
    body { font-family: sans-serif; text-align: center; margin: 1.5rem auto; max-width: 720px; padding: 0 1rem; }
    img { max-width: 100%; height: auto; border: 1px solid #ccc; border-radius: 6px; }
    h3 { margin: 1.6rem 0 .4rem; border-top: 1px solid #ddd; padding-top: 1rem; }
    .row { margin: 1rem 0; }
    button { font-size: 1rem; padding: .5rem 1rem; border: 0; border-radius: 6px; color: #fff; cursor: pointer; background: #6c757d; }
    button:disabled { opacity: .5; cursor: default; }
    #btn-refresh, #btn-save, #btn-server { background: #0d6efd; } #btn-start { background: #198754; } #btn-stop { background: #dc3545; }
    input { font-size: 1rem; width: 5rem; padding: .3rem; text-align: center; }
    #status { font-weight: bold; }
    .block { margin: .6rem auto; max-width: 420px; text-align: left; }
    .crow { display: flex; align-items: center; justify-content: space-between; margin: .5rem 0; }
    .crow .name { flex: 1; }
    .crow button { min-width: 3rem; min-height: 2.75rem; font-size: 1.25rem; padding: 0; }
    .crow .val { min-width: 5.5rem; text-align: center; font-weight: bold; }
    .srow { display: flex; justify-content: space-between; gap: 1rem; margin: .3rem 0; }
    .srow span:last-child { font-weight: bold; text-align: right; }
    .field { display: flex; align-items: center; justify-content: space-between; gap: .6rem; margin: .5rem 0; }
    .field input { flex: 1; width: auto; min-width: 0; text-align: left; box-sizing: border-box; }
    #server-port { flex: 0 0 6rem; }
    .hint { font-size: .85rem; color: #666; margin: .2rem 0; }
    .msg { min-height: 1.3rem; margin: .4rem 0; font-size: .95rem; }
    .warn { color: #b35c00; font-weight: bold; }
  </style>
</head>
<body>
  <h2>📷 ESP32-S3 Camera</h2>
  <div class="row"><img id="preview" src="/preview"></div>
  <div class="row"><button id="btn-refresh">🔄 Refresh preview</button></div>

  <h3>Color</h3>
  <div class="block" id="color-rows"></div>
  <div class="row"><button id="btn-save">💾 Save</button> <button id="btn-reset">↩ Reset to defaults</button></div>
  <div class="msg" id="msg-color"></div>

  <h3>Board status</h3>
  <div class="block">
    <div class="srow"><span>Chip temperature</span><span id="sys-temp">—</span></div>
    <div class="srow"><span>Wi-Fi signal</span><span id="sys-wifi">—</span></div>
    <div class="srow"><span>IP address</span><span id="sys-ip">—</span></div>
    <div class="srow"><span>Uptime</span><span id="sys-uptime">—</span></div>
    <div class="srow"><span>Memory (heap)</span><span id="sys-heap">—</span></div>
    <div class="srow"><span>Memory (PSRAM)</span><span id="sys-psram">—</span></div>
    <div class="srow"><span>Last restart</span><span id="sys-reset">—</span></div>
  </div>
  <div class="row"><button id="btn-sys">🔄 Refresh status</button></div>

  <h3>Upload server</h3>
  <div class="block">
    <div class="field"><label for="server-ip">Server IP</label>
      <input type="text" id="server-ip" inputmode="decimal" placeholder="192.168.0.212" autocomplete="off"></div>
    <div class="field"><label for="server-port">Port</label>
      <input type="number" id="server-port" value="8000" min="1" max="65535"></div>
    <div class="hint" id="server-hint">Photos are sent to http://IP:PORT/upload</div>
  </div>
  <div class="row"><button id="btn-server">💾 Save server</button></div>
  <div class="msg" id="msg-server"></div>

  <h3>Auto capture</h3>
  <div class="row">
    <label for="interval">Interval (sec):</label>
    <input type="number" id="interval" value="30" min="1">
  </div>
  <div class="row">
    <button id="btn-start">▶ Start</button>
    <button id="btn-stop" disabled>⏹ Stop</button>
  </div>
  <p id="status"></p>

  <h3>Current settings</h3>
  <div class="block">
    <div class="srow"><span>Upload URL</span><span id="cur-url">—</span></div>
    <div class="srow"><span>Interval</span><span id="cur-interval">—</span></div>
    <div class="srow"><span>Auto capture</span><span id="cur-auto">—</span></div>
    <div id="cur-color"></div>
    <div class="warn" id="cur-unsaved"></div>
  </div>
  <div class="row"><button id="btn-settings">🔄 Reload settings</button></div>
  <script src="/main.js"></script>
</body>
</html>)HTML";

// Порт static/js/main.js с добавлением цвета, состояния платы, адреса сервера и блока текущих настроек.
static const char MAIN_JS[] PROGMEM = R"JS(document.addEventListener("DOMContentLoaded", function () {
  const preview = document.getElementById("preview");
  const btnRefresh = document.getElementById("btn-refresh");
  const btnStart = document.getElementById("btn-start");
  const btnStop = document.getElementById("btn-stop");
  const btnServer = document.getElementById("btn-server");
  const intervalInput = document.getElementById("interval");
  const status = document.getElementById("status");
  const serverIp = document.getElementById("server-ip");
  const serverPort = document.getElementById("server-port");
  const serverHint = document.getElementById("server-hint");

  function setStatus(text, isError=false) {
    status.textContent = text;
    status.style.color = isError ? "red" : "inherit";
  }

  function setMsg(id, text, isError=false) {
    const el = document.getElementById(id);
    el.textContent = text;
    el.style.color = isError ? "red" : "inherit";
  }

  function nowText() {
    return new Date().toLocaleTimeString();
  }

  function errText(e) {
    return e && e.message ? e.message : String(e);
  }

  // Ошибка HTTP: показываем текст ответа платы
  function checkOk(r) {
    if (r.ok) return r;
    return r.text().then(t => { throw new Error(t || ("HTTP " + r.status)); });
  }

  function refreshPreview() {
    preview.src = "/preview?ts=" + Date.now();
  }

  function updateButtons() {
    fetch("/status")
      .then(r => r.json())
      .then(data => {
        if (data.running) {
          btnStart.disabled = true;
          btnStop.disabled = false;
          setStatus(`Auto capture is running (interval ${data.interval} sec)`);
        } else {
          btnStart.disabled = false;
          btnStop.disabled = true;
          setStatus("Auto capture is stopped");
        }
        btnServer.disabled = data.running;
        serverHint.textContent = data.running
          ? "Stop auto capture to change the server"
          : "Photos are sent to http://IP:PORT/upload";
      })
      .catch(e => {
        setStatus("Failed to get status: " + e, true);
      });
  }

  btnRefresh.addEventListener("click", function () {
    refreshPreview();
  });

  btnStart.addEventListener("click", function () {
    const interval = Math.max(1, parseInt(intervalInput.value || "5"));
    setStatus("Starting auto capture...");
    fetch(`/auto_start?interval=${interval}`).then(r => r.text()).then(t => {
      setStatus(t);
      updateButtons();
      loadSettings(false);
    }).catch(e => {
      setStatus("Failed to start: " + e, true);
    });
  });

  btnStop.addEventListener("click", function () {
    setStatus("Stopping...");
    fetch("/auto_stop").then(r => r.text()).then(t => {
      setStatus(t);
      updateButtons();
      loadSettings(false);
    }).catch(e => {
      setStatus("Failed to stop: " + e, true);
    });
  });

  // ---- Color ----
  const WB_NAMES = ["Auto", "Sunny", "Cloudy", "Office", "Home"];
  const COLOR_PARAMS = [
    { key: "brightness", label: "Brightness", min: -2, max: 2 },
    { key: "contrast", label: "Contrast", min: -2, max: 2 },
    { key: "saturation", label: "Saturation", min: -2, max: 2 },
    { key: "ae_level", label: "Exposure", min: -2, max: 2 },
    { key: "wb_mode", label: "White balance", min: 0, max: 4 },
    { key: "lenc", label: "Lens correction", min: 0, max: 1 }
  ];
  let savedColor = null;
  let liveColor = null;
  let lastSettings = null;
  let previewTimer = null;

  function fmtColor(key, v) {
    if (key === "wb_mode") return WB_NAMES[v] || String(v);
    if (key === "lenc") return v ? "On" : "Off";
    return v > 0 ? "+" + v : String(v);
  }

  const colorBox = document.getElementById("color-rows");
  COLOR_PARAMS.forEach(p => {
    const row = document.createElement("div");
    row.className = "crow";
    row.innerHTML = `<span class="name">${p.label}</span><button class="dec">−</button><span class="val" id="val-${p.key}">—</span><button class="inc">+</button>`;
    colorBox.appendChild(row);
    function step(delta) {
      if (!liveColor) return;
      const next = Math.min(p.max, Math.max(p.min, liveColor[p.key] + delta));
      if (next === liveColor[p.key]) return;
      fetch(`/control?var=${p.key}&val=${next}`)
        .then(checkOk).then(r => r.json())
        .then(data => {
          liveColor = data;
          renderColor();
          renderCurrent();
          setMsg("msg-color", "Applied at " + nowText() + " (not saved yet, press Save)");
          schedulePreview();
        })
        .catch(e => setMsg("msg-color", "Color change failed: " + errText(e), true));
    }
    row.querySelector(".dec").addEventListener("click", () => step(-1));
    row.querySelector(".inc").addEventListener("click", () => step(1));
  });

  function renderColor() {
    if (!liveColor) return;
    COLOR_PARAMS.forEach(p => {
      document.getElementById("val-" + p.key).textContent = fmtColor(p.key, liveColor[p.key]);
    });
  }

  function schedulePreview() {
    clearTimeout(previewTimer);
    previewTimer = setTimeout(refreshPreview, 700);
  }

  document.getElementById("btn-save").addEventListener("click", function () {
    fetch("/camera_save").then(checkOk).then(r => r.text())
      .then(() => loadSettings(false))
      .then(() => {
        const same = savedColor && liveColor && COLOR_PARAMS.every(p => savedColor[p.key] === liveColor[p.key]);
        if (same) setMsg("msg-color", "Saved and confirmed on the board at " + nowText());
        else setMsg("msg-color", "Saved, but the stored values differ from the current ones", true);
      })
      .catch(e => setMsg("msg-color", "Save failed: " + errText(e), true));
  });

  document.getElementById("btn-reset").addEventListener("click", function () {
    fetch("/camera_reset").then(checkOk).then(r => r.json())
      .then(data => {
        liveColor = data;
        renderColor();
        renderCurrent();
        setMsg("msg-color", "Reset to defaults at " + nowText() + " (not saved yet, press Save)");
        schedulePreview();
      })
      .catch(e => setMsg("msg-color", "Reset failed: " + errText(e), true));
  });

  // ---- Current settings (what is stored on the board) ----
  function renderCurrent() {
    if (!lastSettings) return;
    document.getElementById("cur-url").textContent = lastSettings.server.url;
    document.getElementById("cur-interval").textContent = lastSettings.interval + " sec";
    document.getElementById("cur-auto").textContent = lastSettings.running ? "running" : "stopped";
    const box = document.getElementById("cur-color");
    box.innerHTML = "";
    let unsaved = false;
    COLOR_PARAMS.forEach(p => {
      const saved = savedColor ? savedColor[p.key] : null;
      const live = liveColor ? liveColor[p.key] : null;
      let text = saved === null ? "—" : fmtColor(p.key, saved);
      if (saved !== null && live !== null && saved !== live) {
        text += ` (now: ${fmtColor(p.key, live)}, unsaved)`;
        unsaved = true;
      }
      const row = document.createElement("div");
      row.className = "srow";
      row.innerHTML = `<span>${p.label} (saved)</span><span>${text}</span>`;
      box.appendChild(row);
    });
    document.getElementById("cur-unsaved").textContent =
      unsaved ? "⚠ Unsaved color changes: they are lost on restart. Press Save." : "";
  }

  // fillInputs=true: подставить сохранённые значения в поля сервера и интервала
  function loadSettings(fillInputs) {
    return fetch("/settings").then(checkOk).then(r => r.json()).then(s => {
      lastSettings = s;
      savedColor = s.saved;
      liveColor = s.live;
      if (fillInputs) {
        serverIp.value = s.server.ip;
        serverPort.value = s.server.port;
        intervalInput.value = s.interval;
      }
      renderColor();
      renderCurrent();
    }).catch(e => {
      setMsg("msg-color", "Failed to read settings: " + errText(e), true);
      throw e;
    });
  }

  document.getElementById("btn-settings").addEventListener("click", function () {
    loadSettings(false).catch(() => {});
  });

  // ---- Upload server ----
  btnServer.addEventListener("click", function () {
    const ip = serverIp.value.trim();
    const port = serverPort.value.trim() || "8000";
    setMsg("msg-server", "Saving...");
    fetch("/set_server?ip=" + encodeURIComponent(ip) + "&port=" + encodeURIComponent(port))
      .then(checkOk).then(r => r.json())
      .then(() => loadSettings(false))
      .then(() => setMsg("msg-server", "Saved and confirmed on the board at " + nowText() + ": " + lastSettings.server.url))
      .catch(e => setMsg("msg-server", "Save failed: " + errText(e), true));
  });

  // ---- Board status ----
  function fmtUptime(sec) {
    const d = Math.floor(sec / 86400), h = Math.floor(sec % 86400 / 3600), m = Math.floor(sec % 3600 / 60);
    return (d ? d + " d " : "") + h + " h " + m + " min";
  }

  function loadSysInfo() {
    fetch("/sysinfo").then(checkOk).then(r => r.json()).then(d => {
      const temp = document.getElementById("sys-temp");
      temp.textContent = d.temp.toFixed(1) + " °C";
      temp.style.color = d.temp >= 80 ? "#dc3545" : (d.temp >= 70 ? "#fd7e14" : "#198754");
      const q = d.rssi >= -55 ? "excellent" : (d.rssi >= -65 ? "good" : (d.rssi >= -75 ? "weak" : "very weak"));
      document.getElementById("sys-wifi").textContent = d.rssi + " dBm (" + q + ")";
      document.getElementById("sys-ip").textContent = d.ip;
      document.getElementById("sys-uptime").textContent = fmtUptime(d.uptime);
      document.getElementById("sys-heap").textContent = Math.round(d.heap / 1024) + " / " + Math.round(d.heap_total / 1024) + " KB free";
      document.getElementById("sys-psram").textContent = Math.round(d.psram / 1024) + " / " + Math.round(d.psram_total / 1024) + " KB free";
      document.getElementById("sys-reset").textContent = d.reset;
    }).catch(e => setStatus("Failed to get board status: " + errText(e), true));
  }

  document.getElementById("btn-sys").addEventListener("click", loadSysInfo);

  loadSettings(true).catch(() => {});
  loadSysInfo();
  refreshPreview();
  updateButtons();
  setInterval(updateButtons, 3000);
});
)JS";

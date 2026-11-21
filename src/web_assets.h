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
    .row { margin: 1rem 0; }
    button { font-size: 1rem; padding: .5rem 1rem; border: 0; border-radius: 6px; color: #fff; cursor: pointer; }
    button:disabled { opacity: .5; cursor: default; }
    #btn-refresh { background: #0d6efd; } #btn-start { background: #198754; } #btn-stop { background: #dc3545; }
    input { font-size: 1rem; width: 5rem; padding: .3rem; text-align: center; }
    #status { font-weight: bold; }
    .color { margin: 1rem auto; max-width: 420px; text-align: left; }
    .crow { display: flex; align-items: center; justify-content: space-between; margin: .5rem 0; }
    .crow .name { flex: 1; }
    .crow button { background: #6c757d; min-width: 3rem; min-height: 2.75rem; font-size: 1.25rem; padding: 0; }
    .crow .val { min-width: 5.5rem; text-align: center; font-weight: bold; }
    #btn-save { background: #0d6efd; } #btn-reset { background: #6c757d; }
  </style>
</head>
<body>
  <h2>📷 Камера ESP32-S3</h2>
  <div class="row"><img id="preview" src="/preview"></div>
  <div class="row"><button id="btn-refresh">🔄 Обновить превью</button></div>
  <div class="color">
    <div class="crow" data-var="brightness" data-min="-2" data-max="2"><span class="name">Яркость</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="crow" data-var="contrast" data-min="-2" data-max="2"><span class="name">Контраст</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="crow" data-var="saturation" data-min="-2" data-max="2"><span class="name">Насыщенность</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="crow" data-var="ae_level" data-min="-2" data-max="2"><span class="name">Экспозиция</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="crow" data-var="wb_mode" data-min="0" data-max="4"><span class="name">Баланс белого</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="crow" data-var="lenc" data-min="0" data-max="1"><span class="name">Коррекция краёв</span><button class="dec">−</button><span class="val"></span><button class="inc">+</button></div>
    <div class="row"><button id="btn-save">💾 Сохранить</button> <button id="btn-reset">↩ Сбросить</button></div>
  </div>
  <div class="row">
    <label for="interval">Интервал (сек):</label>
    <input type="number" id="interval" value="30" min="1">
  </div>
  <div class="row">
    <button id="btn-start">▶ Старт</button>
    <button id="btn-stop" disabled>⏹ Стоп</button>
  </div>
  <p id="status"></p>
  <script src="/main.js"></script>
</body>
</html>)HTML";

// Порт static/js/main.js; добавлено только заполнение интервала из /status при первой загрузке.
static const char MAIN_JS[] PROGMEM = R"JS(document.addEventListener("DOMContentLoaded", function () {
  const preview = document.getElementById("preview");
  const btnRefresh = document.getElementById("btn-refresh");
  const btnStart = document.getElementById("btn-start");
  const btnStop = document.getElementById("btn-stop");
  const intervalInput = document.getElementById("interval");
  const status = document.getElementById("status");
  let intervalFilled = false;

  function setStatus(text, isError=false) {
    status.textContent = text;
    status.style.color = isError ? "red" : "inherit";
  }

  function refreshPreview() {
    preview.src = "/preview?ts=" + Date.now();
    setStatus("Обновлено превью");
  }

  function updateButtons() {
    fetch("/status")
      .then(r => r.json())
      .then(data => {
        if (!intervalFilled) { intervalInput.value = data.interval; intervalFilled = true; }
        if (data.running) {
          btnStart.disabled = true;
          btnStop.disabled = false;
          setStatus(`Автосъёмка работает (интервал ${data.interval} сек)`);
        } else {
          btnStart.disabled = false;
          btnStop.disabled = true;
          setStatus("Автосъёмка остановлена");
        }
      })
      .catch(e => {
        setStatus("Ошибка получения статуса: " + e, true);
      });
  }

  btnRefresh.addEventListener("click", function () {
    setStatus("Обновление...");
    refreshPreview();
  });

  btnStart.addEventListener("click", function () {
    const interval = Math.max(1, parseInt(intervalInput.value || "5"));
    setStatus("Запуск автосъёмки...");
    fetch(`/auto_start?interval=${interval}`).then(r => r.text()).then(t => {
      setStatus(t);
      updateButtons();
    }).catch(e => {
      setStatus("Ошибка запуска: " + e, true);
    });
  });

  btnStop.addEventListener("click", function () {
    setStatus("Остановка...");
    fetch("/auto_stop").then(r => r.text()).then(t => {
      setStatus(t);
      updateButtons();
    }).catch(e => {
      setStatus("Ошибка остановки: " + e, true);
    });
  });

  // Цветопередача: значения запрашиваются один раз при загрузке и после нажатий кнопок
  const WB_NAMES = ["Авто", "Солнце", "Облачно", "Офис", "Дом"];
  const colorRows = document.querySelectorAll(".crow");
  let colorValues = {};
  let previewTimer = null;

  function showColor() {
    colorRows.forEach(row => {
      const v = colorValues[row.dataset.var];
      let text = v > 0 && row.dataset.var !== "wb_mode" && row.dataset.var !== "lenc" ? "+" + v : String(v);
      if (row.dataset.var === "wb_mode") text = WB_NAMES[v] || v;
      if (row.dataset.var === "lenc") text = v ? "Вкл" : "Выкл";
      row.querySelector(".val").textContent = text;
    });
  }

  function applyColorResponse(r) {
    if (!r.ok) throw new Error("HTTP " + r.status);
    return r.json().then(data => { colorValues = data; showColor(); });
  }

  function schedulePreview() {
    clearTimeout(previewTimer);
    previewTimer = setTimeout(refreshPreview, 700);
  }

  colorRows.forEach(row => {
    const name = row.dataset.var;
    const min = parseInt(row.dataset.min);
    const max = parseInt(row.dataset.max);
    function step(delta) {
      const next = Math.min(max, Math.max(min, (colorValues[name] || 0) + delta));
      if (next === colorValues[name]) return;
      fetch(`/control?var=${name}&val=${next}`)
        .then(applyColorResponse)
        .then(() => { setStatus("Применено (не сохранено)"); schedulePreview(); })
        .catch(e => setStatus("Ошибка настройки цвета: " + e, true));
    }
    row.querySelector(".dec").addEventListener("click", () => step(-1));
    row.querySelector(".inc").addEventListener("click", () => step(1));
  });

  document.getElementById("btn-save").addEventListener("click", function () {
    fetch("/camera_save").then(r => r.text()).then(t => setStatus(t))
      .catch(e => setStatus("Ошибка сохранения: " + e, true));
  });

  document.getElementById("btn-reset").addEventListener("click", function () {
    fetch("/camera_reset").then(applyColorResponse)
      .then(() => { setStatus("Сброшено к значениям по умолчанию (не сохранено)"); schedulePreview(); })
      .catch(e => setStatus("Ошибка сброса: " + e, true));
  });

  fetch("/camera_settings").then(applyColorResponse)
    .catch(e => setStatus("Ошибка получения настроек цвета: " + e, true));

  refreshPreview();
  updateButtons();
  setInterval(updateButtons, 3000);
});
)JS";

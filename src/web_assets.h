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
  </style>
</head>
<body>
  <h2>📷 Камера ESP32-S3</h2>
  <div class="row"><img id="preview" src="/preview"></div>
  <div class="row"><button id="btn-refresh">🔄 Обновить превью</button></div>
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

  refreshPreview();
  updateButtons();
  setInterval(updateButtons, 3000);
});
)JS";

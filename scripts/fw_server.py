#!/usr/bin/env python3
"""Сервер прошивок для кнопки "Check for update" на плате ESP32-S3 CAM.

Отдаёт два файла из папки (по умолчанию firmware_out рядом с проектом):
  /version.json   {"version": "...", "sha256": "...", "size": N}
  /firmware.bin   образ прошивки

Только стандартная библиотека Python 3.8+, ничего устанавливать не нужно.
Без окна:   python fw_server.py --dir firmware_out --port 8080
С окном:    python fw_server_ui.py   (или start_fw_server.bat в корне проекта)

Перед каждой отдачей файла сервер сверяет version.json с firmware.bin (размер и SHA-256).
Если они не совпали, отвечает 500 с пояснением, чтобы плата не скачала испорченную пару файлов.

Класс FirmwareServer можно запускать и останавливать из другой программы (его использует окно).
О происходящем он сообщает через on_event(событие: dict). Типы событий:
  info                      служебное сообщение (text)
  error                     ошибка (text)
  check     ip me           плата запросила version.json (me: адрес этого ПК, по которому она достучалась)
  start     ip me total     началось скачивание прошивки (total байт)
  progress  ip sent total   ход скачивания
  done      ip total secs   скачано целиком
  abort     ip sent total   соединение оборвалось до конца
"""
import argparse
import hashlib
import json
import logging
import os
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ALLOWED = {"/version.json": "application/json", "/firmware.bin": "application/octet-stream"}
CHUNK = 16384
PROGRESS_STEP = 0.5   # не чаще, чем раз в полсекунды сообщать о ходе скачивания
log = logging.getLogger("fw_server")


def default_dir():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware_out"))


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def read_version(directory):
    """Содержимое version.json или None."""
    try:
        with open(os.path.join(directory, "version.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def check_pair(directory):
    """Возвращает текст ошибки или None, если version.json и firmware.bin согласованы."""
    vpath = os.path.join(directory, "version.json")
    bpath = os.path.join(directory, "firmware.bin")
    if not os.path.isfile(vpath) or not os.path.isfile(bpath):
        return "version.json or firmware.bin is missing in " + directory
    try:
        with open(vpath, encoding="utf-8") as f:
            info = json.load(f)
        size, digest = int(info["size"]), str(info["sha256"]).lower()
        str(info["version"])
    except (ValueError, KeyError, OSError) as e:
        return "version.json is not valid: %s" % e
    if os.path.getsize(bpath) != size:
        return "firmware.bin size %d does not match version.json (%d)" % (os.path.getsize(bpath), size)
    if file_sha256(bpath) != digest:
        return "firmware.bin SHA-256 does not match version.json"
    return None


def lan_ip():
    """Адрес этой машины в сети, через который уходит трафик наружу."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def local_ips():
    """Все IPv4-адреса машины (основной первым). Виртуальные адаптеры (WSL, VPN) тоже попадут в список."""
    ips = [lan_ip()]
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.append(info[4][0])
    except OSError:
        pass
    result = []
    for ip in ips:
        if ip.startswith("127.") or ip.startswith("169.254.") or ip in result:
            continue
        result.append(ip)
    result.sort(key=_ip_rank)   # sort устойчивый: при равенстве первым остаётся адрес основного маршрута
    return result or ["127.0.0.1"]


def _ip_rank(ip):
    """Домашние сети (192.168.x, 10.x) вероятнее всего и есть та, где стоит плата; виртуальные адаптеры (WSL, Docker, VPN) идут после."""
    a, b = (int(x) for x in ip.split(".")[:2])
    if a == 192 and b == 168:
        return 0
    if a == 10:
        return 1
    if a == 172 and 16 <= b <= 31:
        return 2
    return 3


class Handler(BaseHTTPRequestHandler):
    directory = "."
    emit = staticmethod(lambda event: None)
    protocol_version = "HTTP/1.1"
    server_version = "fw-server"

    def log_message(self, fmt, *args):
        pass   # события отдаём через emit, стандартную строку лога не пишем

    def send_text(self, code, text):
        body = (text + "\n").encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        ip = self.client_address[0]
        path = self.path.split("?", 1)[0]
        if path == "/":
            return self.send_text(200, "Firmware server. Files: " + ", ".join(sorted(ALLOWED)))
        if path not in ALLOWED:
            return self.send_text(404, "Not found")
        me = self.connection.getsockname()[0]   # адрес этого ПК, по которому достучалась плата
        problem = check_pair(self.directory)
        if problem:
            self.emit({"type": "error", "text": "%s asked for %s, refused: %s" % (ip, path, problem)})
            return self.send_text(500, problem)
        fpath = os.path.join(self.directory, path.lstrip("/"))
        total = os.path.getsize(fpath)
        self.send_response(200)
        self.send_header("Content-Type", ALLOWED[path])
        self.send_header("Content-Length", str(total))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if path == "/version.json":
            self.emit({"type": "check", "ip": ip, "me": me})
        else:
            self.send_firmware(ip, me, fpath, total)
            return
        with open(fpath, "rb") as f:
            self.wfile.write(f.read())

    def send_firmware(self, ip, me, fpath, total):
        self.emit({"type": "start", "ip": ip, "me": me, "total": total})
        sent, began, last = 0, time.time(), 0.0
        try:
            with open(fpath, "rb") as f:
                while True:
                    chunk = f.read(CHUNK)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    sent += len(chunk)
                    now = time.time()
                    if now - last >= PROGRESS_STEP:
                        last = now
                        self.emit({"type": "progress", "ip": ip, "sent": sent, "total": total})
        except OSError:   # плата оборвала соединение (BrokenPipe, ConnectionReset и т.п.)
            self.emit({"type": "abort", "ip": ip, "sent": sent, "total": total})
            return
        self.emit({"type": "progress", "ip": ip, "sent": total, "total": total})
        self.emit({"type": "done", "ip": ip, "total": total, "secs": time.time() - began})


class FirmwareServer:
    """HTTP-сервер прошивок, который можно запускать и останавливать."""

    def __init__(self, directory, port, host="0.0.0.0", on_event=None):
        self.directory = os.path.abspath(directory)
        self.port = port
        self.host = host
        self.on_event = on_event or (lambda event: None)
        self._httpd = None
        self._thread = None

    @property
    def running(self):
        return self._httpd is not None

    def start(self):
        """Открывает порт и запускает обработку запросов. Бросает OSError, если порт занят."""
        if self._httpd:
            return
        if not os.path.isdir(self.directory):
            raise OSError("Folder not found: " + self.directory)
        handler = type("BoundHandler", (Handler,), {
            "directory": self.directory,
            "emit": staticmethod(self.on_event),
        })
        httpd = ThreadingHTTPServer((self.host, self.port), handler)
        httpd.daemon_threads = True
        self._httpd = httpd
        self._thread = threading.Thread(target=httpd.serve_forever, name="fw-server", daemon=True)
        self._thread.start()
        self.on_event({"type": "info", "text": "Server started on port %d, folder %s" % (self.port, self.directory)})

    def stop(self):
        if not self._httpd:
            return
        httpd, self._httpd = self._httpd, None
        httpd.shutdown()
        httpd.server_close()
        self._thread.join(timeout=3)
        self._thread = None
        self.on_event({"type": "info", "text": "Server stopped"})


def console_event(event):
    """Печать событий в консоль для запуска без окна."""
    t = event["type"]
    if t == "check":
        log.info("%s asked for version.json", event["ip"])
    elif t == "start":
        log.info("%s started downloading firmware (%d bytes)", event["ip"], event["total"])
    elif t == "done":
        log.info("%s downloaded firmware in %.1f s", event["ip"], event["secs"])
    elif t == "abort":
        log.warning("%s dropped the connection at %d of %d bytes", event["ip"], event["sent"], event["total"])
    elif t == "error":
        log.error(event["text"])
    elif t == "info":
        log.info(event["text"])


def main():
    ap = argparse.ArgumentParser(description="Firmware server for the ESP32 camera board")
    ap.add_argument("--dir", default=default_dir(), help="folder with firmware.bin and version.json (default: ../firmware_out)")
    ap.add_argument("--port", type=int, default=8080, help="port to listen on (default: 8080)")
    ap.add_argument("--host", default="0.0.0.0", help="address to listen on (default: all interfaces)")
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", datefmt="%H:%M:%S")
    server = FirmwareServer(args.dir, args.port, args.host, console_event)
    try:
        server.start()
    except OSError as e:
        sys.exit("Cannot start: %s" % e)

    problem = check_pair(server.directory)
    if problem:
        log.warning("Not ready yet: %s", problem)
    else:
        log.info("Serving firmware %s", read_version(server.directory)["version"])
    log.info("Enter on the board (Updates tab): IP %s, port %d", ", ".join(local_ips()), args.port)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        server.stop()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Окно управления сервером прошивок: кнопки Start/Stop, адрес сервера и ход скачивания платой.

Запуск: двойной щелчок по start_fw_server.bat в корне проекта или `python scripts/fw_server_ui.py`.
Нужен только Python с tkinter (входит в обычную установку Python для Windows).
"""
import os
import queue
import sys
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fw_server  # noqa: E402

POLL_MS = 150          # как часто окно разбирает события сервера
FILES_MS = 3000        # как часто перечитываем состояние папки с прошивкой
DONE_SHOW_SEC = 3600   # результат последней загрузки висит в окне, пока не будет новой

GOOD, WARN, BAD, IDLE = "#1a7f4b", "#a75a00", "#c62d3a", "#6b7785"


def mb(n):
    return "%.2f MB" % (n / 1048576.0)


class App:
    def __init__(self, root, directory, port):
        self.root = root
        self.events = queue.Queue()   # события приходят из потоков сервера, обрабатываем в окне
        self.server = None
        self.files_sig = None
        root.title("Firmware server")
        root.geometry("560x700")
        root.minsize(520, 620)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.dir_var = tk.StringVar(value=directory)
        self.port_var = tk.StringVar(value=str(port))
        self.status_var = tk.StringVar(value="Stopped")
        self.addr_var = tk.StringVar(value="-")
        self.others_var = tk.StringVar(value="")
        self.fw_var = tk.StringVar(value="-")
        self.xfer_var = tk.StringVar(value="Server is stopped")
        self.xfer_ip_var = tk.StringVar(value="")

        pad = {"padx": 12, "pady": 6}
        frm = ttk.Frame(root)
        frm.pack(fill="both", expand=True)

        # Состояние и кнопки
        top = ttk.Frame(frm)
        top.pack(fill="x", **pad)
        self.dot = tk.Canvas(top, width=16, height=16, highlightthickness=0)
        self.dot.pack(side="left")
        self.dot_id = self.dot.create_oval(2, 2, 14, 14, fill=IDLE, outline="")
        ttk.Label(top, textvariable=self.status_var, font=("Segoe UI", 12, "bold")).pack(side="left", padx=8)
        self.btn_stop = ttk.Button(top, text="Stop server", command=self.stop, state="disabled")
        self.btn_stop.pack(side="right")
        self.btn_start = ttk.Button(top, text="Start server", command=self.start)
        self.btn_start.pack(side="right", padx=6)

        # Адрес для платы
        box = ttk.LabelFrame(frm, text="Enter on the board (Updates tab)")
        box.pack(fill="x", **pad)
        row = ttk.Frame(box)
        row.pack(fill="x", padx=10, pady=(8, 2))
        ttk.Label(row, textvariable=self.addr_var, font=("Consolas", 15, "bold")).pack(side="left")
        ttk.Button(row, text="Copy IP", command=self.copy_ip).pack(side="right")
        ttk.Label(box, textvariable=self.others_var, foreground=IDLE, wraplength=480).pack(anchor="w", padx=10, pady=(0, 8))

        # Настройки
        opt = ttk.LabelFrame(frm, text="Settings")
        opt.pack(fill="x", **pad)
        r1 = ttk.Frame(opt)
        r1.pack(fill="x", padx=10, pady=(8, 2))
        ttk.Label(r1, text="Port", width=8).pack(side="left")
        self.port_entry = ttk.Entry(r1, textvariable=self.port_var, width=8)
        self.port_entry.pack(side="left")
        r2 = ttk.Frame(opt)
        r2.pack(fill="x", padx=10, pady=(2, 8))
        ttk.Label(r2, text="Folder", width=8).pack(side="left")
        ttk.Label(r2, textvariable=self.dir_var, wraplength=330).pack(side="left", fill="x", expand=True)
        self.btn_browse = ttk.Button(r2, text="Browse...", command=self.browse)
        self.btn_browse.pack(side="right")

        # Прошивка в папке
        fw = ttk.LabelFrame(frm, text="Firmware in the folder")
        fw.pack(fill="x", **pad)
        self.fw_label = ttk.Label(fw, textvariable=self.fw_var, wraplength=480, justify="left")
        self.fw_label.pack(anchor="w", padx=10, pady=8)

        # Скачивание платой
        xf = ttk.LabelFrame(frm, text="Board download")
        xf.pack(fill="x", **pad)
        self.xfer_label = ttk.Label(xf, textvariable=self.xfer_var, font=("Segoe UI", 11, "bold"), wraplength=480)
        self.xfer_label.pack(anchor="w", padx=10, pady=(8, 2))
        ttk.Label(xf, textvariable=self.xfer_ip_var, foreground=IDLE).pack(anchor="w", padx=10)
        self.bar = ttk.Progressbar(xf, maximum=100, length=300)
        self.bar.pack(fill="x", padx=10, pady=(4, 10))

        # Журнал
        lg = ttk.LabelFrame(frm, text="Events")
        lg.pack(fill="both", expand=True, **pad)
        self.log = tk.Text(lg, height=8, state="disabled", wrap="word", font=("Consolas", 9))
        sb = ttk.Scrollbar(lg, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log.pack(fill="both", expand=True, padx=(8, 0), pady=6)
        self.log.tag_configure("warn", foreground=WARN)
        self.log.tag_configure("bad", foreground=BAD)
        self.log.tag_configure("good", foreground=GOOD)

        self.ips = fw_server.local_ips()
        self.render_address(False)
        self.refresh_files(force=True)
        self.pump()
        self.files_tick()

    # ---- Запуск и остановка ----
    def start(self):
        try:
            port = int(self.port_var.get())
            if not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            messagebox.showerror("Port", "Port must be a number from 1 to 65535.")
            return
        server = fw_server.FirmwareServer(self.dir_var.get(), port, on_event=self.events.put)
        try:
            server.start()
        except OSError as e:
            messagebox.showerror("Cannot start", str(e))
            return
        self.server = server
        self.ips = fw_server.local_ips()
        self.set_running(True)

    def stop(self):
        if self.server:
            self.server.stop()
            self.server = None
        self.set_running(False)

    def set_running(self, running):
        self.btn_start.configure(state="disabled" if running else "normal")
        self.btn_stop.configure(state="normal" if running else "disabled")
        self.port_entry.configure(state="disabled" if running else "normal")
        self.btn_browse.configure(state="disabled" if running else "normal")
        self.status_var.set("Running" if running else "Stopped")
        self.dot.itemconfigure(self.dot_id, fill=GOOD if running else IDLE)
        self.render_address(running)
        self.set_xfer("Waiting for the board...", "", 0, IDLE) if running else self.set_xfer("Server is stopped", "", 0, IDLE)
        self.root.title("Firmware server" + (" - running" if running else ""))

    def render_address(self, running):
        port = self.port_var.get()
        self.addr_var.set("%s : %s" % (self.ips[0], port))
        if len(self.ips) > 1:
            self.others_var.set("Other addresses of this PC: %s. Use the one in the same network as the board."
                                % ", ".join(self.ips[1:]))
        else:
            self.others_var.set("The address of this PC in your network." if running else "")

    def copy_ip(self):
        self.root.clipboard_clear()
        self.root.clipboard_append(self.ips[0])
        self.log_line("Copied %s to the clipboard" % self.ips[0])

    def browse(self):
        d = filedialog.askdirectory(initialdir=self.dir_var.get(), title="Folder with firmware.bin and version.json")
        if d:
            self.dir_var.set(os.path.abspath(d))
            self.refresh_files(force=True)

    # ---- Состояние папки ----
    def refresh_files(self, force=False):
        d = self.dir_var.get()
        sig = []
        for name in ("firmware.bin", "version.json"):
            try:
                st = os.stat(os.path.join(d, name))
                sig.append((st.st_mtime, st.st_size))
            except OSError:
                sig.append(None)
        if not force and sig == self.files_sig:
            return   # файлы не менялись, SHA-256 заново не считаем
        self.files_sig = sig
        problem = fw_server.check_pair(d)
        info = fw_server.read_version(d)
        if problem:
            self.fw_var.set("Not ready: " + problem)
            self.fw_label.configure(foreground=BAD)
        else:
            self.fw_var.set("Version %s, %s. Files match." % (info["version"], mb(info["size"])))
            self.fw_label.configure(foreground=GOOD)

    def files_tick(self):
        self.refresh_files()
        self.root.after(FILES_MS, self.files_tick)

    # ---- События сервера ----
    def set_xfer(self, text, ip, percent, color):
        self.xfer_var.set(text)
        self.xfer_ip_var.set(ip)
        self.bar["value"] = percent
        self.xfer_label.configure(foreground=color)

    def log_line(self, text, tag=None):
        self.log.configure(state="normal")
        self.log.insert("end", time.strftime("%H:%M:%S  ") + text + "\n", tag)
        lines = int(self.log.index("end-1c").split(".")[0])
        if lines > 300:
            self.log.delete("1.0", "%d.0" % (lines - 300))
        self.log.see("end")
        self.log.configure(state="disabled")

    def adopt_address(self, me):
        """Плата достучалась до этого ПК по адресу me: показываем его первым, это точно верный адрес."""
        if me and not me.startswith("127.") and me != self.ips[0]:   # запрос с этого же ПК адрес не подтверждает
            self.ips = [me] + [ip for ip in self.ips if ip != me]
            self.render_address(True)
            self.log_line("The board reached this PC at %s, shown above" % me)

    def handle(self, e):
        t = e["type"]
        self.adopt_address(e.get("me"))
        if t == "info":
            self.log_line(e["text"])
        elif t == "error":
            self.log_line(e["text"], "bad")
        elif t == "check":
            self.log_line("%s asked which version is available" % e["ip"])
        elif t == "start":
            self.log_line("%s started downloading the firmware (%s)" % (e["ip"], mb(e["total"])), "warn")
            self.set_xfer("Downloading firmware...", "to board " + e["ip"], 0, WARN)
            self.root.title("Firmware server - DOWNLOADING")
        elif t == "progress":
            pct = int(e["sent"] * 100 / e["total"]) if e["total"] else 0
            self.set_xfer("Downloading firmware... %d%%" % pct,
                          "to board %s: %s of %s" % (e["ip"], mb(e["sent"]), mb(e["total"])), pct, WARN)
            self.root.title("Firmware server - downloading %d%%" % pct)
        elif t == "done":
            self.log_line("%s downloaded the whole firmware in %.1f s" % (e["ip"], e["secs"]), "good")
            self.set_xfer("Done: the board has the file", "board %s, %.1f s. It now verifies the file and restarts."
                          % (e["ip"], e["secs"]), 100, GOOD)
            self.root.title("Firmware server - running")
        elif t == "abort":
            self.log_line("%s dropped the connection at %s of %s" % (e["ip"], mb(e["sent"]), mb(e["total"])), "bad")
            self.set_xfer("Interrupted", "board %s stopped at %s of %s. The board keeps its old firmware."
                          % (e["ip"], mb(e["sent"]), mb(e["total"])), 0, BAD)
            self.root.title("Firmware server - running")

    def pump(self):
        try:
            while True:
                self.handle(self.events.get_nowait())
        except queue.Empty:
            pass
        self.root.after(POLL_MS, self.pump)

    def on_close(self):
        self.stop()
        self.root.destroy()


def main():
    root = tk.Tk()
    try:
        App(root, fw_server.default_dir(), 8080)
    except Exception as e:  # окно без консоли: покажем ошибку, а не закроемся молча
        messagebox.showerror("Firmware server", "Failed to start the window:\n%s" % e)
        raise
    root.mainloop()


if __name__ == "__main__":
    main()

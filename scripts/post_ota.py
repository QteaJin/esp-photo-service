"""После настройки сборки:
1. Для загрузки по Wi-Fi (upload_protocol = espota) подставляет пароль из include/secrets.h,
   чтобы пароль хранился в одном месте.
2. После сборки кладёт firmware.bin и version.json в папку для раздачи (по умолчанию firmware_out/).
   Эту папку нужно выложить на сервер прошивок, откуда плата заберёт обновление по кнопке.
"""
Import("env")  # noqa: F821  (объект предоставляет PlatformIO)

import hashlib
import json
import os
import re
import shutil

project_dir = env["PROJECT_DIR"]  # noqa: F821


def read_ota_password():
    path = os.path.join(project_dir, "include", "secrets.h")
    try:
        with open(path, encoding="utf-8") as f:
            m = re.search(r'#define\s+OTA_PASSWORD\s+"([^"]*)"', f.read())
        return m.group(1) if m else None
    except OSError:
        return None


# --- 1. Пароль для загрузки по Wi-Fi ---
if env.subst("$UPLOAD_PROTOCOL") == "espota":  # noqa: F821
    existing = " ".join(str(x) for x in env.get("UPLOADERFLAGS", []))  # noqa: F821
    if "--auth" not in existing:
        password = read_ota_password()
        if password:
            env.Append(UPLOADERFLAGS=["--auth=" + password])  # noqa: F821
        else:
            print("WARNING: OTA_PASSWORD not found in include/secrets.h, upload will ask for none")


# --- 2. Публикация прошивки для кнопки "Check for update" ---
def publish(source, target, env):  # noqa: F811
    bin_path = str(target[0])
    out_dir = os.path.join(project_dir, env.GetProjectOption("custom_fw_publish_dir", "firmware_out"))
    os.makedirs(out_dir, exist_ok=True)
    with open(bin_path, "rb") as f:
        data = f.read()
    shutil.copyfile(bin_path, os.path.join(out_dir, "firmware.bin"))
    info = {
        "version": env.subst("$FW_VERSION"),
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }
    with open(os.path.join(out_dir, "version.json"), "w", encoding="utf-8") as f:
        json.dump(info, f)
    print("Published to %s: %s, %d bytes" % (out_dir, info["version"], info["size"]))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", publish)  # noqa: F821

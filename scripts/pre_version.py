"""Перед сборкой: вычисляет версию прошивки и передаёт её в код как FW_VERSION.

Версия = дата и время сборки + короткий хэш коммита, например 20261124-1402-844ec97.
Время входит в версию, чтобы любая новая сборка отличалась от установленной,
даже если изменения ещё не закоммичены.
"""
Import("env")  # noqa: F821  (объект предоставляет PlatformIO)

import datetime
import subprocess


def git_hash():
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=env["PROJECT_DIR"],  # noqa: F821
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return "nogit"


version = datetime.datetime.now().strftime("%Y%m%d-%H%M") + "-" + git_hash()
env.Replace(FW_VERSION=version)  # noqa: F821  (читает post_ota.py)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
print("Firmware version:", version)

"""Build optionally, then run the Reader integration firmware in ESP32-S3 QEMU.

Run this script from an activated ESP-IDF environment. Generated build, flash,
and log files stay under ``ci/reader_tests`` and are ignored by Git.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
PROJECT = HERE / "firmware"


def idf_python() -> Path:
    configured = os.environ.get("ESP_PYTHON")
    return Path(configured).expanduser().resolve() if configured else Path(sys.executable)


def build_firmware() -> None:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("IDF_PATH is not set; activate the ESP-IDF environment first")
    idf_py = Path(idf_path) / "tools" / "idf.py"
    if not idf_py.is_file():
        raise RuntimeError(f"idf.py was not found under IDF_PATH: {idf_py}")
    log_path = HERE / "test_build.log"
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            [str(idf_python()), str(idf_py), "-B", "build", "build"],
            cwd=PROJECT,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
            text=True,
        )
    if result.returncode:
        tail = log_path.read_text(encoding="utf-8", errors="replace")[-6000:]
        raise RuntimeError(f"ESP-IDF test firmware build failed:\n{tail}")


def resolve_qemu() -> Path:
    configured = os.environ.get("QEMU_SYSTEM_XTENSA")
    if configured:
        executable = Path(configured).expanduser().resolve()
        if executable.is_file():
            return executable
        raise RuntimeError(f"QEMU_SYSTEM_XTENSA does not name a file: {executable}")

    on_path = shutil.which("qemu-system-xtensa")
    if on_path:
        return Path(on_path).resolve()

    tools_root = Path(os.environ.get("IDF_TOOLS_PATH", Path.home() / ".espressif"))
    for name in ("qemu-system-xtensa.exe", "qemu-system-xtensa"):
        candidates = sorted(tools_root.glob(f"qemu-xtensa/*/qemu/bin/{name}"), reverse=True)
        if candidates:
            return candidates[0].resolve()
    raise RuntimeError(
        "qemu-system-xtensa was not found; put it on PATH or set QEMU_SYSTEM_XTENSA"
    )


def create_flash_image() -> Path:
    build = PROJECT / "build"
    settings_path = build / "flasher_args.json"
    if not settings_path.is_file():
        raise RuntimeError("test firmware is not built; run with --build first")
    settings = json.loads(settings_path.read_text(encoding="utf-8"))
    flash = HERE / "qemu_flash.bin"
    command = [
        str(idf_python()),
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "merge_bin",
        "--output",
        str(flash),
        "--fill-flash-size",
        "4MB",
    ]
    for address, source in settings["flash_files"].items():
        command.extend((address, str(build / source)))
    subprocess.run(command, check=True)
    return flash


def run_qemu(flash: Path, timeout_seconds: float) -> None:
    log_path = HERE / "qemu.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            [
                str(resolve_qemu()),
                "-M",
                "esp32s3",
                "-nographic",
                "-drive",
                f"file={flash.as_posix()},if=mtd,format=raw",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + timeout_seconds
            previous = ""
            while time.monotonic() < deadline:
                content = log_path.read_text(encoding="utf-8", errors="replace")
                if content != previous:
                    for line in content[len(previous) :].splitlines():
                        if any(
                            marker in line
                            for marker in ("PASS", "SIZES", "TEST_FAILURE", "FRAME_POOL")
                        ):
                            print(line, flush=True)
                    previous = content
                if "ALL_READER_TESTS_PASSED" in content:
                    return
                if (
                    "TEST_FAILURE" in content
                    or "Guru Meditation" in content
                    or process.poll() is not None
                ):
                    raise RuntimeError(f"QEMU tests failed:\n{content[-6000:]}")
                time.sleep(0.2)
            raise TimeoutError(f"QEMU test timeout:\n{previous[-6000:]}")
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", action="store_true", help="build the test firmware first")
    parser.add_argument("--timeout", type=float, default=240.0, help="QEMU timeout in seconds")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    if args.build:
        build_firmware()
    run_qemu(create_flash_image(), args.timeout)


if __name__ == "__main__":
    main()

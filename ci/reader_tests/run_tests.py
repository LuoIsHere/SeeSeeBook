"""Build optionally, then run the Reader integration firmware in ESP32-S3 QEMU.

Run this script from an activated ESP-IDF environment. Generated build, flash,
and log files stay under ``ci/reader_tests`` and are ignored by Git.
"""

from __future__ import annotations

import argparse
import binascii
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zlib


HERE = Path(__file__).resolve().parent
PROJECT = HERE / "firmware"
PREVIEW_WIDTH = 480
PREVIEW_HEIGHT = 800
PREVIEW_ROW_BYTES = PREVIEW_WIDTH // 8


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


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_preview_png(path: Path, packed: bytes) -> None:
    expected = PREVIEW_ROW_BYTES * PREVIEW_HEIGHT
    if len(packed) != expected:
        raise RuntimeError(
            f"preview {path.stem} has {len(packed)} bytes; expected {expected}"
        )
    rows = bytearray()
    for y in range(PREVIEW_HEIGHT):
        rows.append(0)
        source = packed[y * PREVIEW_ROW_BYTES : (y + 1) * PREVIEW_ROW_BYTES]
        for x in range(PREVIEW_WIDTH):
            rows.append(0 if source[x // 8] & (1 << (x % 8)) else 255)
    header = struct.pack(">IIBBBBB", PREVIEW_WIDTH, PREVIEW_HEIGHT, 8, 0, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
        + png_chunk(b"IEND", b"")
    )


def extract_previews() -> list[Path]:
    log_path = HERE / "qemu.log"
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    previews: dict[str, list[str]] = {}
    current: str | None = None
    for line in lines:
        if line.startswith("UI_PREVIEW_BEGIN "):
            current = line.removeprefix("UI_PREVIEW_BEGIN ").strip()
            previews[current] = []
        elif line.startswith("UI_PREVIEW_END "):
            name = line.removeprefix("UI_PREVIEW_END ").strip()
            if current != name:
                raise RuntimeError(f"preview marker mismatch: {current!r} != {name!r}")
            current = None
        elif current is not None:
            previews[current].append(line.strip())
    if current is not None:
        raise RuntimeError(f"preview {current!r} did not have an end marker")
    output = HERE.parents[1] / "inDocs" / "previews" / "v0.3-ui"
    output.mkdir(parents=True, exist_ok=True)
    paths: list[Path] = []
    for name, rows in previews.items():
        if len(rows) != PREVIEW_HEIGHT or any(
            len(row) != PREVIEW_ROW_BYTES * 2 for row in rows
        ):
            raise RuntimeError(f"preview {name!r} has invalid row geometry")
        path = output / f"{name}.png"
        write_preview_png(path, bytes.fromhex("".join(rows)))
        paths.append(path)
    return paths


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
    for preview in extract_previews():
        print(f"PREVIEW {preview}")


if __name__ == "__main__":
    main()

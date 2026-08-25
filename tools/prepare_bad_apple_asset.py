#!/usr/bin/env python3
"""Build the firmware-only Bad Apple silhouette asset from a pinned source video.

The source MP4 is downloaded only as conversion input and is not committed to
this repository. Copyright in the original Bad Apple!! music/PV remains with
its respective rights holders; the project license does not relicense it.
"""
from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

WIDTH = 168
HEIGHT = 126
FPS = 10
FRAME_COUNT = 2190
PIXELS_PER_FRAME = WIDTH * HEIGHT
PACKED_FRAME_BYTES = (WIDTH // 8) * HEIGHT
MAX_COMPRESSED_BYTES = 3_500_000
SOURCE_COMMIT = "ea6954fcca5172ab8b32c6bfc68f8d042c1d59a8"
SOURCE_BLOB_SHA1 = "fb8ba0b0969a508e73d2421c6571c12e0fe7b103"
SOURCE_URL = (
    "https://raw.githubusercontent.com/pingvortex/ESP32-Bad-Apple/"
    f"{SOURCE_COMMIT}/vid.mp4"
)
ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = ROOT / ".badapple-cache"
SOURCE_PATH = CACHE_DIR / "source.mp4"
GENERATED_DIR = ROOT / "src" / "generated"
HEADER_PATH = GENERATED_DIR / "BadAppleAsset.h"
CPP_PATH = GENERATED_DIR / "BadAppleAsset.cpp"


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def acquire_source() -> bytes:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    if SOURCE_PATH.exists():
        data = SOURCE_PATH.read_bytes()
        if git_blob_sha1(data) == SOURCE_BLOB_SHA1:
            return data
        SOURCE_PATH.unlink()

    request = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "t-display-gp-build"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    actual = git_blob_sha1(data)
    if actual != SOURCE_BLOB_SHA1:
        raise RuntimeError(f"Bad Apple source blob mismatch: {actual}")
    SOURCE_PATH.write_bytes(data)
    return data


def extract_gray_frames() -> bytes:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required to prepare the Bad Apple asset")
    command = [
        ffmpeg,
        "-v", "error",
        "-i", str(SOURCE_PATH),
        "-an",
        "-vf", f"fps={FPS},scale={WIDTH}:{HEIGHT}:flags=area",
        "-frames:v", str(FRAME_COUNT),
        "-pix_fmt", "gray",
        "-f", "rawvideo",
        "pipe:1",
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {result.stderr.decode('utf-8', errors='replace')}")
    expected = PIXELS_PER_FRAME * FRAME_COUNT
    if len(result.stdout) != expected:
        raise RuntimeError(
            f"expected exactly {FRAME_COUNT} frames ({expected} gray bytes), got {len(result.stdout)} bytes"
        )
    return result.stdout


def pack_frame(gray: memoryview) -> bytes:
    packed = bytearray(PACKED_FRAME_BYTES)
    out = 0
    for y in range(HEIGHT):
        row = y * WIDTH
        for byte_x in range(WIDTH // 8):
            value = 0
            base = row + byte_x * 8
            for bit in range(8):
                if gray[base + bit] >= 128:
                    value |= 0x80 >> bit
            packed[out] = value
            out += 1
    return bytes(packed)


def put_varuint(out: bytearray, value: int) -> None:
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)


def read_varuint(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while pos < len(data) and shift <= 28:
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, pos
        shift += 7
    raise RuntimeError("invalid Bad Apple delta varint")


def encode_delta(previous: bytes, current: bytes) -> bytes:
    delta = bytes(a ^ b for a, b in zip(previous, current))
    encoded = bytearray()
    cursor = 0
    size = len(delta)
    while cursor < size:
        start = cursor
        while start < size and delta[start] == 0:
            start += 1
        if start == size:
            break

        end = start
        while end < size:
            if delta[end] != 0:
                end += 1
                continue
            zero_start = end
            while end < size and delta[end] == 0:
                end += 1
            if end - zero_start >= 4:
                end = zero_start
                break

        put_varuint(encoded, start - cursor)
        put_varuint(encoded, end - start)
        encoded.extend(delta[start:end])
        cursor = end
    return bytes(encoded)


def decode_delta(previous: bytes, encoded: bytes) -> bytes:
    frame = bytearray(previous)
    input_pos = 0
    cursor = 0
    while input_pos < len(encoded):
        skip, input_pos = read_varuint(encoded, input_pos)
        if skip > len(frame) - cursor:
            raise RuntimeError("Bad Apple delta skip exceeds frame")
        cursor += skip
        literal, input_pos = read_varuint(encoded, input_pos)
        if literal > len(frame) - cursor or literal > len(encoded) - input_pos:
            raise RuntimeError("Bad Apple delta literal exceeds frame/input")
        for index in range(literal):
            frame[cursor + index] ^= encoded[input_pos + index]
        cursor += literal
        input_pos += literal
    return bytes(frame)


def write_array(handle, values, per_line: int = 16, formatter=str) -> None:
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        handle.write("  " + ", ".join(formatter(v) for v in chunk) + ",\n")


def emit_asset(first_frame: bytes, offsets: list[int], delta_data: bytes) -> None:
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(
        "#pragma once\n\n"
        "#include <cstddef>\n#include <cstdint>\n\n"
        "#include \"BadApplePlayback.h\"\n\n"
        "namespace BadAppleAsset {\n"
        "extern const uint8_t FIRST_FRAME[BadApplePlayback::FRAME_BYTES];\n"
        "extern const uint32_t DELTA_OFFSETS[BadApplePlayback::FRAME_COUNT];\n"
        "extern const uint8_t DELTA_DATA[];\n"
        "extern const size_t DELTA_DATA_SIZE;\n"
        "}  // namespace BadAppleAsset\n",
        encoding="utf-8",
    )
    with CPP_PATH.open("w", encoding="utf-8") as handle:
        handle.write("#include \"BadAppleAsset.h\"\n\n#include <Arduino.h>\n\nnamespace BadAppleAsset {\n")
        handle.write("const uint8_t FIRST_FRAME[BadApplePlayback::FRAME_BYTES] PROGMEM = {\n")
        write_array(handle, first_frame, formatter=lambda v: f"0x{v:02X}")
        handle.write("};\n\nconst uint32_t DELTA_OFFSETS[BadApplePlayback::FRAME_COUNT] PROGMEM = {\n")
        write_array(handle, offsets, per_line=10)
        handle.write("};\n\nconst uint8_t DELTA_DATA[] PROGMEM = {\n")
        write_array(handle, delta_data, formatter=lambda v: f"0x{v:02X}")
        handle.write("};\n\nconst size_t DELTA_DATA_SIZE = sizeof(DELTA_DATA);\n}  // namespace BadAppleAsset\n")


def main() -> int:
    acquire_source()
    gray = extract_gray_frames()
    view = memoryview(gray)
    frames: list[bytes] = []
    for index in range(FRAME_COUNT):
        start = index * PIXELS_PER_FRAME
        frames.append(pack_frame(view[start:start + PIXELS_PER_FRAME]))

    first = frames[0]
    offsets = [0]
    payload = bytearray()
    previous = first
    for frame_index, current in enumerate(frames[1:], start=1):
        encoded = encode_delta(previous, current)
        reconstructed = decode_delta(previous, encoded)
        if reconstructed != current:
            raise RuntimeError(f"Bad Apple delta round-trip mismatch at frame {frame_index}")
        payload.extend(encoded)
        offsets.append(len(payload))
        previous = current

    if len(offsets) != FRAME_COUNT:
        raise RuntimeError("Bad Apple offset table length mismatch")
    total_asset = len(first) + len(payload) + 4 * len(offsets)
    if total_asset > MAX_COMPRESSED_BYTES:
        raise RuntimeError(
            f"Bad Apple asset {total_asset} bytes exceeds {MAX_COMPRESSED_BYTES}-byte firmware budget"
        )
    emit_asset(first, offsets, bytes(payload))
    print(
        f"Bad Apple asset: {WIDTH}x{HEIGHT}, {FRAME_COUNT} frames @ {FPS} fps, "
        f"packed={PACKED_FRAME_BYTES * FRAME_COUNT} bytes, asset={total_asset} bytes, "
        "delta-roundtrip=OK"
    )
    print(f"generated {HEADER_PATH.relative_to(ROOT)} and {CPP_PATH.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Bad Apple asset preparation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)

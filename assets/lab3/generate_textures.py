"""Regenerate the six original, seamless PNG textures using Python's standard library.

The application loads the committed PNG files with libpng; this script is optional.
"""

import math
from pathlib import Path
import struct
import zlib


SIZE = 256
TAU = math.tau


def mix(a, b, t):
    return tuple(round(x * (1 - t) + y * t) for x, y in zip(a, b))


def pixel(name, x, y):
    u, v = x / SIZE, y / SIZE
    grain = 0.5 + 0.5 * math.sin(x * 1.7 + y * 2.3)
    if name == "checker":
        return (224, 219, 190) if (x // 32 + y // 32) % 2 else (40, 66, 94)
    if name == "brick":
        row = y // 32
        bx, by = (x + (row % 2) * 32) % 64, y % 32
        if bx < 3 or by < 3:
            return (183, 173, 151)
        return mix((122, 44, 28), (208, 100, 61), 0.3 + 0.4 * grain)
    if name == "wood":
        warp = 0.15 * math.sin(TAU * v) + 0.06 * math.sin(TAU * (2 * v + u))
        rings = 0.5 + 0.5 * math.sin(TAU * (8 * u + warp))
        return mix((65, 29, 13), (196, 133, 61), 0.2 + 0.7 * rings + 0.1 * grain)
    if name == "marble":
        warp = math.sin(TAU * (2 * u + v)) + 0.35 * math.cos(TAU * (u - 3 * v))
        veins = (0.5 + 0.5 * math.sin(TAU * (2 * u + v) + warp)) ** 12
        return mix((218, 233, 231), (32, 76, 95), veins)
    if name == "tiles":
        bx, by = x % 64, y % 64
        if bx < 3 or by < 3:
            return (207, 213, 200)
        base = (36, 134, 142) if (x // 64 + y // 64) % 2 else (37, 76, 114)
        return mix(base, (108, 188, 188), 0.22 * math.sin(math.pi * bx / 64) * math.sin(math.pi * by / 64))
    wave = 0.5 + 0.5 * math.sin(TAU * (3 * u + 2 * v) + 1.3 * math.sin(TAU * (u - v)))
    return mix((32, 35, 126), (245, 150, 58), wave)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def write_png(name):
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)  # PNG filter: None
        for x in range(SIZE):
            rows.extend(pixel(name, x, y))
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    Path(__file__).with_name(name + ".png").write_bytes(png)


if __name__ == "__main__":
    for texture in ("checker", "brick", "wood", "marble", "tiles", "waves"):
        write_png(texture)

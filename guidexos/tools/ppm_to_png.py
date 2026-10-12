#!/usr/bin/env python3
"""Convert a binary P6 PPM screenshot to PNG using only Python's stdlib."""

from __future__ import annotations

import binascii
import struct
import sys
import zlib
from pathlib import Path


def read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    size = len(data)
    while offset < size:
        byte = data[offset]
        if byte in b" \t\r\n\v\f":
            offset += 1
        elif byte == ord("#"):
            newline = data.find(b"\n", offset)
            if newline < 0:
                raise ValueError("unterminated PPM header comment")
            offset = newline + 1
        else:
            break

    start = offset
    while offset < size and data[offset] not in b" \t\r\n\v\f":
        offset += 1
    if start == offset:
        raise ValueError("unexpected end of PPM header")

    token = data[start:offset]
    if offset < size:
        separator = data[offset]
        offset += 1
        if separator == ord("\r") and offset < size and data[offset] == ord("\n"):
            offset += 1
    return token, offset


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(
        ">I", binascii.crc32(body) & 0xFFFFFFFF
    )


def convert(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    offset = 0
    magic, offset = read_token(data, offset)
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    maximum_token, offset = read_token(data, offset)

    if magic != b"P6":
        raise ValueError(f"expected binary P6 PPM, got {magic!r}")
    width = int(width_token)
    height = int(height_token)
    maximum = int(maximum_token)
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError("only positive-size RGB PPM with maxval 255 is supported")

    expected_size = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected_size:
        raise ValueError(
            f"PPM pixel data has {len(pixels)} bytes; expected {expected_size}"
        )

    row_bytes = width * 3
    scanlines = b"".join(
        b"\x00" + pixels[row * row_bytes : (row + 1) * row_bytes]
        for row in range(height)
    )
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(scanlines, level=6))
        + png_chunk(b"IEND", b"")
    )
    destination.write_bytes(png)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: ppm_to_png.py INPUT.ppm OUTPUT.png", file=sys.stderr)
        return 2
    try:
        convert(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError) as error:
        print(f"ppm_to_png: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Export a read-back 640x480 RGBA4444 bitmap, without drawing UI content."""
import argparse
import binascii
import struct
import zlib
from pathlib import Path


def bitmap_png(raw):
    if len(raw) != 640 * 480 * 2:
        raise ValueError('expected exactly 614400 bytes for the functional 640x480 RGBA4444 bitmap')
    rgba = bytearray()
    for y in range(480):
        rgba.append(0)  # PNG row filter: none.
        for (pixel,) in struct.iter_unpack('<H', raw[y * 1280:(y + 1) * 1280]):
            rgba.extend(((pixel >> 12) * 17, (pixel >> 8 & 15) * 17,
                         (pixel >> 4 & 15) * 17, (pixel & 15) * 17))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data))

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 640, 480, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rgba)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bitmap', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(bitmap_png(args.bitmap.read_bytes()))


if __name__ == '__main__':
    main()

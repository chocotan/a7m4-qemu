#!/usr/bin/env python3
"""Export a read-back RGBA4444 bitmap, optionally composited over black for display."""
import argparse
import binascii
import struct
import zlib
from pathlib import Path


def bitmap_png(raw, *, black_background=False):
    if len(raw) != 640 * 480 * 2:
        raise ValueError('expected exactly 614400 bytes for the functional 640x480 RGBA4444 bitmap')
    pixels = bytearray()
    for y in range(480):
        pixels.append(0)  # PNG row filter: none.
        for (pixel,) in struct.iter_unpack('<H', raw[y * 1280:(y + 1) * 1280]):
            rgba = ((pixel >> 12) * 17, (pixel >> 8 & 15) * 17,
                    (pixel >> 4 & 15) * 17, (pixel & 15) * 17)
            if black_background:
                pixels.extend((channel * rgba[3] + 127) // 255 for channel in rgba[:3])
            else:
                pixels.extend(rgba)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data))

    color_type = 2 if black_background else 6
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 640, 480, 8, color_type, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bitmap', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--black-background', action='store_true',
                        help='export an opaque RGB preview over black; default keeps the original alpha')
    args = parser.parse_args()
    args.output.write_bytes(bitmap_png(args.bitmap.read_bytes(), black_background=args.black_background))


if __name__ == '__main__':
    main()

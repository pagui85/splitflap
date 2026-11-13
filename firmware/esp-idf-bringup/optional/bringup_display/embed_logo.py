# SPDX-License-Identifier: Apache-2.0
"""Convert the existing 48x48 SF ICO frame to LVGL's little-endian ARGB8888."""
from pathlib import Path
import struct
import sys

icon = Path(sys.argv[1]).read_bytes()
reserved, kind, count = struct.unpack_from("<HHH", icon)
if (reserved, kind) != (0, 1):
    raise ValueError("Expected an ICO file")
for index in range(count):
    width, height, _, _, planes, bits, length, offset = struct.unpack_from(
        "<BBBBHHII", icon, 6 + index * 16)
    if (width, height, planes, bits) != (48, 48, 1, 32):
        continue
    frame = icon[offset:offset + length]
    if struct.unpack_from("<IiiHHI", frame) != (40, 48, 96, 1, 32, 0):
        raise ValueError("Expected an uncompressed BGRA icon frame")
    stride = 48 * 4
    rows = [frame[40 + y * stride:40 + (y + 1) * stride] for y in reversed(range(48))]
    if any(len(row) != stride for row in rows):
        raise ValueError("Truncated icon frame")
    with Path(sys.argv[2]).open("w") as out:
        out.write("alignas(4) constexpr uint8_t kLogoPixels[] = {\n")
        for row in rows:
            out.write("    " + ", ".join(f"0x{byte:02x}" for byte in row) + ",\n")
        out.write("};\n")
    break
else:
    raise ValueError("Missing the 48x48 SF icon frame")

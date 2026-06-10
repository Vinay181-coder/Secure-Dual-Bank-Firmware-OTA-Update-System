#!/usr/bin/env python3
import sys
import struct

def stm32_crc32(data):
    """
    Matches STM32F4 hardware CRC unit exactly.
    Polynomial : 0x04C11DB7  (non-reflected)
    Init       : 0xFFFFFFFF
    Input      : little-endian uint32_t words (native ARM memory order)
    No final XOR, no bit reflection.
    """
    crc = 0xFFFFFFFF

    padded = bytearray(data)
    while len(padded) % 4 != 0:
        padded += b'\x00'

    for i in range(0, len(padded), 4):
        # '<I' = little-endian uint32 — same byte order ARM uses for CRC->DR = src[i]
        # NO byte-swap after this — that was the bug in the previous version
        word = struct.unpack('<I', padded[i:i+4])[0]

        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = (crc << 1) ^ 0x04C11DB7
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF

    return crc


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python compute_crc32.py <firmware.bin> [version]")
        sys.exit(1)

    filepath = sys.argv[1]
    version  = int(sys.argv[2]) if len(sys.argv) >= 3 else 1

    with open(filepath, 'rb') as f:
        data = f.read()

    crc  = stm32_crc32(data)
    size = len(data)

    print(f"File:    {filepath}")
    print(f"Size:    {size} bytes")
    print(f"CRC32:   0x{crc:08X}  ({crc})")
    print(f"First 16 bytes: " + " ".join(f"{b:02X}" for b in data[:16]))
    print()
    print("── Job document ──────────────────────────────")
    print(f"""{{
  "firmwareUrl": "PASTE_PRESIGNED_URL_HERE",
  "version": 20,
  "size": {size},
  "crc32": {crc}
}}""")
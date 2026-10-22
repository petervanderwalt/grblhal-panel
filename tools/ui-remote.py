"""Capture the live RGB565 LCD framebuffer or inject one LVGL tap over USB UART."""
import argparse
import binascii
from pathlib import Path
import struct
import time
import zlib

def decode_row(encoded, width):
    if len(encoded) % 8:
        raise ValueError("Invalid RLE row")
    row = bytearray()
    for offset in range(0, len(encoded), 8):
        count = int(encoded[offset:offset + 4], 16)
        pixel = int(encoded[offset + 4:offset + 8], 16)
        if count == 0 or len(row) // 2 + count > width:
            raise ValueError("RLE run exceeds row")
        row.extend(struct.pack("<H", pixel) * count)
    if len(row) != width * 2:
        raise ValueError("Incomplete row")
    return row

def png_bytes(raw, width, height):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", binascii.crc32(kind + payload) & 0xffffffff))
    if len(raw) != width * height * 2:
        raise ValueError("Invalid framebuffer size")
    rgb = bytearray()
    for y in range(height):
        rgb.append(0)
        for x in range(width):
            pixel = struct.unpack_from("<H", raw, (y * width + x) * 2)[0]
            r, g, b = (pixel >> 11) & 31, (pixel >> 5) & 63, pixel & 31
            rgb.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rgb)) + chunk(b"IEND", b""))

def self_test():
    assert decode_row("0002f8000001001f", 3) == b"\x00\xf8\x00\xf8\x1f\x00"
    for bad in ("0000f800", "0004f800", "0001f800", "x"):
        try:
            decode_row(bad, 3)
        except ValueError:
            pass
        else:
            raise AssertionError(bad)
    data = png_bytes(struct.pack("<HHH", 0xf800, 0x07e0, 0x001f), 3, 1)
    offset = 8
    idat = b""
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind, payload = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + size]
        assert binascii.crc32(kind + payload) & 0xffffffff == struct.unpack_from(">I", data, offset + 8 + size)[0]
        if kind == b"IDAT":
            idat += payload
        offset += 12 + size
    assert zlib.decompress(idat) == b"\x00\xff\x00\x00\x00\xff\x00\x00\x00\xff"
    print("RLE validation and PNG color/CRC tests passed")


def exchange(port, command, output=None, point=None):
    cmd = "UG SHOT" if command == "shot" else f"UG TAP {point[0]} {point[1]}"
    port.write(("\n" + cmd + "\n").encode("ascii"))
    deadline = time.monotonic() + 180
    width = height = crc = None
    rows = []
    while time.monotonic() < deadline:
        line = port.readline().strip()
        marker = line.find(b"@UG")
        if marker < 0:
            if line.startswith(b"ESP-ROM:"):
                raise RuntimeError("Device reset during command")
            continue
        line = line[marker:].decode("ascii")
        if line.startswith("@UGERR"):
            raise RuntimeError(line)
        if command == "tap" and line == "@UGOK TAP":
            print(line)
            return
        if command != "shot":
            continue
        if line.startswith("@UGSHOT "):
            _, w, h, crc_text = line.split()
            width, height, crc = int(w), int(h), int(crc_text, 16)
            print(f"Receiving {width}x{height}...", flush=True)
            if not (1 <= width <= 4096 and 1 <= height <= 4096):
                raise ValueError("Invalid framebuffer dimensions")
            rows = []
        elif line.startswith("@UGROW ") and width is not None:
            _, y, encoded = line.split()
            if int(y) != len(rows) or len(rows) >= height:
                raise ValueError("Missing or out-of-order screenshot row")
            rows.append(decode_row(encoded, width))
        elif line == "@UGEND" and width is not None:
            raw = b"".join(rows)
            if len(rows) != height or zlib.crc32(raw) & 0xffffffff != crc:
                raise ValueError("Screenshot incomplete or CRC mismatch")
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(png_bytes(raw, width, height))
            print(f"Saved {output.resolve()} ({width}x{height}, CRC {crc:08x})")
            return
    raise TimeoutError("No complete response from UI remote firmware")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM12")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("self-test")
    sub.add_parser("session", help="Keep the serial connection open; accept shot PATH, tap X Y, quit")
    shot = sub.add_parser("shot")
    shot.add_argument("output", type=Path)
    tap = sub.add_parser("tap", help="Sends real UI input; disconnect CNC before testing")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    args = parser.parse_args()
    if args.command == "self-test":
        self_test()
        return
    import serial
    with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        # Probe an invalid, harmless command until the application owns UART0.
        ready = False
        for attempt in range(15):
            port.write(b"\nUG SYNC\n")
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                if b"@UGERR command" in port.readline():
                    ready = True
                    break
            if ready:
                break
        if not ready:
            raise TimeoutError("UI remote console not ready")
        time.sleep(0.2)
        port.reset_input_buffer()
        if args.command == "session":
            import sys
            for line in sys.stdin:
                parts = line.strip().split(maxsplit=1)
                if not parts:
                    continue
                if parts[0] == "quit":
                    break
                if parts[0] == "shot" and len(parts) == 2:
                    exchange(port, "shot", Path(parts[1]))
                elif parts[0] == "tap" and len(parts) == 2:
                    point = tuple(map(int, parts[1].split()))
                    if len(point) != 2:
                        raise ValueError("Expected tap X Y")
                    exchange(port, "tap", point=point)
                    time.sleep(0.2)
                else:
                    raise ValueError("Expected shot PATH, tap X Y, or quit")
        elif args.command == "shot":
            exchange(port, "shot", args.output)
        else:
            exchange(port, "tap", point=(args.x, args.y))

if __name__ == "__main__":
    main()

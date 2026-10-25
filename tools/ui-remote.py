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

def buffered_lines(port):
    """Batch available UART bytes instead of issuing a Windows read per character."""
    pending = bytearray()
    raw_read, raw_reset = port.read, port.reset_input_buffer
    def readline():
        while True:
            end = pending.find(b"\n")
            if end >= 0:
                line = bytes(pending[:end + 1])
                del pending[:end + 1]
                return line
            block = raw_read(min(16384, port.in_waiting or 1))
            if not block:
                line = bytes(pending)
                pending.clear()
                return line
            pending.extend(block)
    def reset():
        pending.clear()
        raw_reset()
    port.readline, port.reset_input_buffer = readline, reset


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
    class DamagedFrame:
        def __init__(self, damaged=b'@UGROW 0 \xa5\n'):
            self.lines = [b'@UGSHOT 1 2 00000000\n', damaged, b'@UGROW 1 00010000\n', b'@UGEND\n']
        def write(self, _): pass
        def readline(self):
            assert self.lines, 'Damaged frame did not finish cleanly'
            return self.lines.pop(0)
    port = DamagedFrame()
    try:
        exchange(port, 'shot')
    except ValueError as error:
        assert 'non-ASCII' in str(error) and not port.lines
    else:
        raise AssertionError('Accepted a damaged frame')
    for damaged in (b'@UGROW 0 00010000 SDK log\n', b'@UGROW 0 00000000\n', b'@UGROW 5 00010000\n'):
        port = DamagedFrame(damaged)
        try:
            exchange(port, 'shot')
        except ValueError:
            assert not port.lines, 'Rejected frame left UART data ahead of cleanup commands'
        else:
            raise AssertionError('Accepted a malformed screenshot row')
    class BufferedPort:
        def __init__(self): self.data = b"first\nsecond\n"; self.reads = 0
        @property
        def in_waiting(self): return len(self.data)
        def read(self, count):
            self.reads += 1
            result, self.data = self.data[:count], self.data[count:]
            return result
        def reset_input_buffer(self): self.data = b""
    port = BufferedPort()
    buffered_lines(port)
    assert port.readline() == b"first\n" and port.reads == 1
    port.reset_input_buffer()
    assert port.readline() == b"", "Reset kept stale buffered UART data"
    print("RLE validation, buffered UART reads and PNG color/CRC tests passed")


def exchange(port, command, output=None, point=None):
    cmd = "UG SHOT" if command == "shot" else f"UG TAP {point[0]} {point[1]}"
    port.write(("\n" + cmd + "\n").encode("ascii"))
    deadline = time.monotonic() + 180
    width = height = crc = None
    rows = []
    diagnostics = []
    packet_error = None
    while time.monotonic() < deadline:
        line = port.readline().strip()
        marker = line.find(b"@UG")
        if marker < 0:
            if line.startswith(b"ESP-ROM:"):
                reset_log = [line.decode(errors="replace")]
                for _ in range(12):
                    following = port.readline().strip()
                    if following:
                        reset_log.append(following.decode(errors="replace"))
                raise RuntimeError("Device reset during command\n" + "\n".join(diagnostics + reset_log))
            if line:
                diagnostics.append(line.decode("utf-8", errors="replace"))
                diagnostics = diagnostics[:128]
            continue
        try:
            line = line[marker:].decode("ascii")
        except UnicodeDecodeError:
            if command != "shot": raise
            packet_error = "Screenshot contains non-ASCII data"
            continue  # Drain to END before restoring touch; never save the damaged frame.
        if line.startswith("@UGDBG"):
            diagnostics = (diagnostics + [line])[:128]
        if line.startswith("@UGERR"):
            raise RuntimeError(line)
        if command == "tap" and line == "@UGOK TAP":
            print(line)
            return
        if command != "shot":
            continue
        if packet_error and line != "@UGEND":
            continue
        if line.startswith("@UGSHOT "):
            _, w, h, crc_text = line.split()
            width, height, crc = int(w), int(h), int(crc_text, 16)
            print(f"Receiving {width}x{height}...", flush=True)
            if not (1 <= width <= 4096 and 1 <= height <= 4096):
                raise ValueError("Invalid framebuffer dimensions")
            rows = []
        elif line.startswith("@UGROW ") and width is not None:
            if packet_error:
                continue
            try:
                _, y, encoded = line.split()
                if int(y) != len(rows) or len(rows) >= height:
                    raise ValueError("Missing or out-of-order screenshot row")
                rows.append(decode_row(encoded, width))
            except ValueError as error:
                packet_error = "Invalid screenshot row: " + str(error)
                continue  # Drain the rejected frame before sending touch-restoration commands.
        elif line == "@UGEND" and width is not None:
            if packet_error:
                raise ValueError(packet_error)
            raw = b"".join(rows)
            if len(rows) != height or zlib.crc32(raw) & 0xffffffff != crc:
                raise ValueError("Screenshot incomplete or CRC mismatch")
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(png_bytes(raw, width, height))
            print(f"Saved {output.resolve()} ({width}x{height}, CRC {crc:08x})")
            return
    raise TimeoutError("No complete response from UI remote firmware\n" + "\n".join(diagnostics))

def debug(port, command):
    port.write(("\nUG " + command + "\n").encode("ascii"))
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        line = port.readline().strip()
        if line.startswith(b"ESP-ROM:"):
            raise RuntimeError("Device reset during command")
        if line.startswith(b"@UGTEST"):
            print(line.decode(), flush=True)
        if line.startswith(b"@UGERR"):
            raise RuntimeError(line.decode())
        if line.startswith((b"@UGOK", b"@UGDATA")):
            result = line.decode()
            print(result, flush=True)
            return result
    raise TimeoutError(command)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM12")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("self-test")
    for name in ("test", "state", "last", "prefs"):
        sub.add_parser(name)
    demo = sub.add_parser("demo")
    demo.add_argument("mode", choices=("on", "off"))
    rx = sub.add_parser("rx")
    rx.add_argument("line")
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
        buffered_lines(port)
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
                elif parts[0] in ("demo", "touch", "rx", "test", "state", "last", "prefs", "hold", "swipe"):
                    verb = parts[0].upper()
                    payload = parts[1] if len(parts) == 2 else ""
                    if verb in ("DEMO", "TOUCH"):
                        payload = payload.upper()
                    debug(port, (verb + " " + payload).strip())
                    time.sleep(0.2)
                else:
                    raise ValueError("Expected shot PATH, tap X Y, hold X Y MS, swipe X1 Y1 X2 Y2, demo on/off, rx LINE, test, state, last or quit")
        elif args.command == "shot":
            exchange(port, "shot", args.output)
        elif args.command == "tap":
            exchange(port, "tap", point=(args.x, args.y))
        else:
            suffix = args.mode.upper() if args.command == "demo" else args.line if args.command == "rx" else ""
            debug(port, (args.command.upper() + " " + suffix).strip())

if __name__ == "__main__":
    main()

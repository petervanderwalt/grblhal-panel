"""Check saved Wi-Fi reconnects after RTS resets, without supplying credentials."""
import argparse
import json
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="COM12")
parser.add_argument("--cycles", type=int, default=3)
args = parser.parse_args()

with serial.Serial(port=None, baudrate=115200, timeout=.3, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = args.port
    port.open()
    try:
        for cycle in range(args.cycles):
            port.reset_input_buffer()
            port.rts = True
            time.sleep(.15)
            port.rts = False
            start = time.monotonic()
            poll = start + 5
            last = {}
            previous_status = ""
            while time.monotonic() - start < 45:
                if time.monotonic() >= poll:
                    port.write(b"\nUG WIFI STATE\n")
                    poll = time.monotonic() + 1
                line = port.readline().decode(errors="replace").strip()
                if not line.startswith("@UGDATA "):
                    continue
                last = json.loads(line.removeprefix("@UGDATA "))
                if last.get("status", "").startswith("Retrying") and last["status"] != previous_status:
                    print(f"Reboot {cycle + 1}: {last['status']}", flush=True)
                previous_status = last.get("status", "")
                if last.get("connected") and last.get("ip"):
                    print(f"PASS reboot {cycle + 1}: {last['ssid']} / {last['ip']} "
                          f"in {time.monotonic() - start:.1f}s", flush=True)
                    break
            else:
                raise AssertionError(f"No automatic connection: {last}")
    finally:
        port.write(b"\nUG DEMO ON\nUG TOUCH ON\n")
        port.flush()

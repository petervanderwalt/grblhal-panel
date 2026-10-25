"""Capture the real LCD for documentation; machine feedback uses UART-off preview.

Wi-Fi captures use the actual radio and briefly test a failed connection before
restoring the supplied network. Supply its password through PANEL_WIFI_PASSWORD.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import time

import serial

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--output", type=Path, default=Path("captures/gallery"))
    parser.add_argument("--section", choices=("core", "wifi", "all"), default="core")
    parser.add_argument("--ssid", help="Network to test and restore for Wi-Fi captures")
    args = parser.parse_args()
    password = os.environ.get("PANEL_WIFI_PASSWORD")
    if args.section != "core" and (not args.ssid or not password):
        parser.error("Wi-Fi capture requires --ssid and PANEL_WIFI_PASSWORD")

    args.output.mkdir(parents=True, exist_ok=True)
    with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port, \
            (args.output / "serial.log").open("ab") as trace:
        original_readline = port.readline

        def traced_readline(*a, **kw):
            line = original_readline(*a, **kw)
            trace.write(line)
            trace.flush()
            return line

        port.readline = traced_readline
        port.dtr = port.rts = False
        port.port = args.port
        port.open()
        for _ in range(15):
            port.write(b"\nUG SYNC\n")
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                if b"@UGERR command" in port.readline():
                    break
            else:
                continue
            break
        else:
            raise TimeoutError("Console sync")
        time.sleep(.2)
        port.reset_input_buffer()

        def command(text):
            return remote.debug(port, text)

        def data(verb):
            return json.loads(command(verb).removeprefix("@UGDATA "))

        def tap(x, y):
            remote.exchange(port, "tap", point=(x, y))
            time.sleep(.4)

        def shot(name):
            time.sleep(.5)
            remote.exchange(port, "shot", args.output / (name + ".png"))

        def page(index):
            tap((128, 384, 896, 640)[index], 564)
            assert data("PREFS")["page"] == index

        def idle():
            command("RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>")
            time.sleep(.4)

        def wait_wifi(key, seconds=45):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                state = data("WIFI STATE")
                if not state[key]:
                    return state
                time.sleep(.5)
            raise TimeoutError("Wi-Fi " + key)

        def choose_network():
            state = data("WIFI STATE")
            index = state["networks"].index(args.ssid)
            # shortcut: target one of the four visible rows; add scrolling for weaker networks.
            if index >= 4:
                raise RuntimeError("Gallery Wi-Fi target must be one of the first four scan results")
            tap(300, 233 + index * 66)

        def connect(secret):
            command("WIFI PASSWORD " + secret.encode("utf-8").hex())
            tap(844, 259)
            return wait_wifi("connecting")

        try:
            command("TOUCH OFF")
            command("DEMO ON")
            time.sleep(1)
            original_inches = data("PREFS")["inches"]
            if data("PREFS").get("actuator_mode"):
                raise RuntimeError("Select CNC mode before capturing the CNC gallery")
            if args.section in ("wifi", "all"):
                page(2)
                tap(800, 28)
                if not data("WIFI STATE")["scanning"]:
                    tap(682, 166)
                shot("wifi-scanning")
                wait_wifi("scanning")
                shot("wifi-networks")
                choose_network()
                shot("wifi-password")  # Empty field: no credential in the image.
                tap(844, 259)
                shot("wifi-password-validation")
                failed = connect("capture-invalid-password")
                assert not failed["connected"], "The failure test unexpectedly connected"
                shot("wifi-failed")
                tap(512, 409)
                page(2)
                tap(800, 28)
                choose_network()
                connected = connect(password)
                assert connected["connected"], connected
                shot("wifi-connected")
                tap(512, 409)

            if args.section in ("core", "all"):
                page(2)
                tap(761, 448)
                page(0)
                idle()
                tap(631, 184)
                shot("panel-jog")
                tap(885, 184)
                shot("panel-jog-continuous")
                tap(631, 184)
                command("RX <Hold:0|MPos:125,84.5,-12|FS:0,0>")
                shot("panel-jog-disabled")
                idle()
                tap(72, 346)
                shot("panel-home-confirm")
                tap(342, 409)
                tap(432, 346)
                shot("panel-zero-confirm")
                tap(342, 409)
                tap(130, 432)
                tap(720, 154)  # Corner mode retains tool-diameter editing.
                shot("panel-probe")
                tap(280, 272)
                shot("panel-numpad")
                tap(380, 459)
                tap(916, 82)

                page(2)
                shot("panel-settings")
                command("SWIPE 500 460 500 230")
                shot("panel-settings-probe")
                command("SWIPE 500 440 500 320")
                shot("panel-settings-probe-lower")
                command("SWIPE 500 460 500 160")
                command("SWIPE 500 460 500 160")
                shot("panel-settings-uart")
                page(0)
                page(2)
                tap(901, 448)
                shot("panel-settings-inch")
                page(0)
                shot("panel-jog-inch")
                page(2)
                tap(761, 448)

                page(1)
                if data("SD STATE")["local"]:
                    page(2)
                    tap(830, 266)
                    tap(830, 350)
                    page(1)
                assert not data("SD STATE")["local"]
                tap(888, 112)
                assert data("STATE")["files"] == 3
                shot("panel-files")
                tap(922, 189)
                shot("panel-run-confirm")
                tap(630, 409)
                command("RX <Run|SD:42.5,/bracket.nc|MPos:125,84.5,-12|FS:800,12000|Ov:100,100,100>")
                shot("panel-job")
                tap(374, 370)
                shot("panel-stop-confirm")
                tap(342, 409)
                tap(130, 370)
                shot("panel-job-paused")
                tap(130, 370)
                command("RX [MSG:Program End]")
                idle()
                assert data("PREFS")["page"] == 5
                shot("panel-complete")
                tap(512, 366)
                command("RX ALARM:1")
                shot("panel-alarm")
                tap(630, 409)
                idle()
                command("RX error:20")
                shot("panel-error")
                tap(630, 409)
                idle()

            page(2)
            tap(901 if original_inches else 761, 448)
            page(1)
            tap(888, 112)
            print("Gallery captured; preview remains on Files. Review every PNG before publishing.", flush=True)
        finally:
            # A rejected screenshot can still be transmitting; wait for its queued cleanup.
            port.write(b"\nUG TOUCH ON\n")
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                line = port.readline()
                if b"@UGOK TOUCH" in line or line.startswith(b"ESP-ROM:"):
                    break
            else:
                raise TimeoutError("Restore physical touch; synchronize and send UG TOUCH ON")


if __name__ == "__main__":
    main()

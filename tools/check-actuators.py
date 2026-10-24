"""Exercise Actuator mode on the physical LCD with the controller UART disconnected."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="COM12")
parser.add_argument("--no-captures", action="store_true")
parser.add_argument("--configure-demo", action="store_true", help="Replace actuator settings with development examples")
args = parser.parse_args()
if not args.configure_demo:
    parser.error("Use --configure-demo to allow writing the Door/Camera/Vent/Lift test settings")
spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = args.port
    port.open()
    for _ in range(20):
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
        time.sleep(.6)

    def shot(name):
        if not args.no_captures:
            time.sleep(.6)
            remote.exchange(port, "shot", Path("captures") / (name + ".png"))

    def idle(homed=15, four=True):
        command(f"RX <Idle|MPos:0,-75,-50" + (",-25" if four else "") +
                f"|WCO:0,0,0" + (",0" if four else "") + f"|H:0,{homed}|FS:0,0>")
        time.sleep(.4)

    def settings():
        tap(256, 564)  # First tab in either application mode; resets Settings scroll.
        tap(848, 564)
        assert data("PREFS")["page"] == 2

    def configure(axis, values, style, capture=False):
        settings()
        command("SWIPE 500 460 500 310")
        time.sleep(.6)
        if axis >= 2:
            command("SWIPE 500 460 500 284")
            time.sleep(.6)
        tap(830, 347 + (axis % 2) * 88)
        if axis == 3:
            tap(717, 48)  # Development examples use linear A.
        for index, value in enumerate(values):
            command(f"ACTUATOR FIELD {index} " + value.encode().hex())
        tap((317, 447, 577, 707)[style], 280)
        if capture:
            shot("panel-actuator-config")
        tap(882, 280)
        assert data("PREFS")["page"] == 2

    try:
        command("TOUCH OFF")
        command("TEST")
        command("DEMO ON")
        command("RX $376=0")
        time.sleep(1)
        settings()
        tap(761, 256)
        tap(764, 160)
        assert not data("PREFS")["actuator_mode"]
        tap(512, 564)
        assert data("PREFS")["page"] == 1
        tap(848, 564)
        tap(910, 160)
        assert data("PREFS")["actuator_mode"]
        tap(256, 564)
        idle(homed=0)
        last = command("LAST")
        tap(133, 270)
        assert command("LAST") == last, "Unhomed axis accepted a move"
        shot("panel-actuators-unhomed")
        configure(0, ("Door", "Toe", "Oop", "0", "-200", "120"), 0, True)
        configure(1, ("Camera", "Start", "End", "0", "-300", "240"), 1)
        configure(2, ("Vent", "Down", "Up", "0", "-100", "100"), 2)
        configure(3, ("Lift", "Lower", "Raise", "0", "-150", "180"), 0)
        settings()
        tap(901, 256)
        command("SWIPE 500 460 500 310")
        time.sleep(.6)
        tap(830, 347)
        tap(882, 280)  # Saving unchanged inch-displayed values must preserve calibration.
        settings()
        tap(761, 256)
        tap(256, 564)
        idle()
        shot("panel-actuators")
        tap(133, 270)
        assert "X-200.000 F120.0" in command("LAST")
        idle()
        command("SWIPE 452 390 452 300")
        time.sleep(.4)  # Release queues the command; allow the UART worker to send it.
        last = command("LAST")
        assert "$J=G53 G90 G21 Y" in last and "F240.0" in last, last
        command("RX <Jog|MPos:0,-100,-50,-25|H:0,15>")
        time.sleep(.3)
        tap(383, 464)
        assert "0x85" in command("LAST"), "Actuator Stop did not cancel the jog"
        idle()
        tap(633, 332)
        assert "Z-50.000 F100.0" in command("LAST"), "Midpoint preset used the wrong position"
        idle()
        tap(883, 270)
        assert "A-150.000 F180.0" in command("LAST"), "Fourth-axis control failed"
        idle()
        tap(883, 145)
        tap(630, 409)
        assert not (data("STATE")["homed"] & 8), "Home did not invalidate A position"
        idle()
        idle(homed=7, four=False)
        last = command("LAST")
        tap(883, 270)
        assert command("LAST") == last, "Missing A axis accepted a move"
        idle()
        settings()
        command("RX <Jog|MPos:0,-75,-50,-25|H:0,15>")
        time.sleep(.3)
        tap(764, 160)
        assert data("PREFS")["actuator_mode"], "Mode changed while motion was active"
        tap(512, 409)
        idle()
        for _ in range(3):
            settings()
            tap(764, 160)
            assert not data("PREFS")["actuator_mode"]
            tap(910, 160)
            assert data("PREFS")["actuator_mode"]
        settings()
        shot("panel-settings-actuator-mode")
        command("SWIPE 500 460 500 310")
        shot("panel-actuator-settings")
        tap(256, 564)
        idle()
        print("PASS: mode switching, editable configuration, layouts, endpoint moves, release slider, Stop, presets, A-axis homing and missing-axis interlock", flush=True)
    finally:
        command("TOUCH ON")

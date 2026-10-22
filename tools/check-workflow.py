"""Exercise the pendant workflow on COM12 with the controller UART disconnected."""
import importlib.util
import json
from pathlib import Path
import sys
import time
import serial

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = "COM12"
    port.open()
    for attempt in range(15):
        port.write(b"\nUG SYNC\n")
        if b"@UGERR command" in port.readline():
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
        time.sleep(.35)

    def shot(name):
        time.sleep(.4)
        if "--no-captures" not in sys.argv:
            remote.exchange(port, "shot", Path("captures") / (name + ".png"))

    def idle():
        command("RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>")
        time.sleep(.3)

    def page(expected):
        assert data("PREFS")["page"] == expected

    def number(value):
        tap(512, 459)  # Clear
        keys = {"7": (380, 195), "8": (512, 195), "9": (644, 195),
                "4": (380, 259), "5": (512, 259), "6": (644, 259),
                "1": (380, 323), "2": (512, 323), "3": (644, 323),
                ".": (380, 387), "0": (512, 387)}
        for digit in str(value):
            tap(*keys[digit])
        tap(644, 459)  # Apply

    try:
        command("TOUCH OFF")
        command("TEST")
        command("DEMO ON")
        time.sleep(1)
        tap(848, 564)
        page(2)
        original_diameter = data("PREFS")["diameter"]
        tap(901, 160)
        assert data("PREFS")["inches"] == 1
        shot("panel-settings-inch")
        command("SWIPE 500 460 500 160")
        time.sleep(.5)
        command("SWIPE 500 460 500 160")
        time.sleep(.5)
        shot("panel-settings-uart")
        tap(176, 564)
        page(0)
        tap(631, 184)
        prefs = data("PREFS")
        distance = prefs["step_mm"]
        tap(794, 358)  # X+
        assert f"X{distance:.3f}" in command("LAST"), "Inch jog was not converted to mm"
        assert "$J=G91 G21" in command("LAST")
        idle()
        shot("panel-jog-inch")
        tap(130, 432)
        tap(280, 272)
        number(".25")
        assert abs(data("PREFS")["diameter"] - 6.35) < .001
        tap(916, 118)
        tap(848, 564)
        tap(761, 160)
        assert data("PREFS")["inches"] == 0
        shot("panel-settings")
        tap(176, 564)
        shot("panel-jog")
        tap(130, 432)
        tap(280, 272)
        number(original_diameter)
        assert data("PREFS")["diameter"] == original_diameter
        shot("panel-probe")
        command("RX error:20")
        time.sleep(.3)
        tap(630, 409)  # A new error replaces the open probe modal.
        assert data("STATE")["error"] == 0
        tap(130, 432)
        tap(916, 118)  # Close probe
        page(0)
        tap(512, 564)
        page(1)
        tap(888, 112)
        assert data("STATE")["files"] == 3
        shot("panel-files")
        tap(922, 189)  # First row Run
        assert not data("STATE")["job"], "Run skipped confirmation"
        tap(630, 409)
        page(3)
        assert data("STATE")["job"]
        command("RX <Run|SD:42.5,/bracket.nc|MPos:125,84.5,-12|FS:800,12000|Ov:100,100,100>")
        time.sleep(.3)
        tap(176, 564)
        page(3)  # Jog cannot be reached during a job.
        tap(130, 370)
        assert "0x21" in command("LAST")
        tap(130, 370)
        assert "0x7E" in command("LAST")
        command("SWIPE 742 291 790 291")  # Feed override, commit on release
        assert command("LAST").endswith(("0x91", "0x93"))
        time.sleep(.7)
        command("SWIPE 742 401 698 401")  # Spindle override
        assert command("LAST").endswith(("0x9B", "0x9D"))
        shot("panel-job")
        command("RX [MSG:Program End]")
        time.sleep(.3)
        page(3)  # Message alone does not prove motion has stopped.
        idle()
        page(4)
        shot("panel-complete")
        tap(512, 366)
        page(0)
        # An explicitly stopped job must never display Job Complete.
        command("RX <Run|SD:5,/bracket.nc|Ov:100,100,100>")
        time.sleep(.3)
        page(3)
        tap(374, 370)
        time.sleep(.7)
        page(0)
        assert not data("STATE")["job"]
        command("RX <Run|SD:5,/bracket.nc|Ov:100,100,100>")
        time.sleep(.3)
        command("RX ALARM:1")
        time.sleep(.3)
        shot("panel-alarm")
        tap(342, 409)  # Dismiss the fault dialog
        tap(374, 370)  # Job Stop remains below the DRO
        time.sleep(.7)
        page(0)
        assert not data("STATE")["job"]
        command("RX error:20")
        time.sleep(.3)
        shot("panel-error")
        tap(630, 409)  # Acknowledge error
        assert data("STATE")["error"] == 0
        print("PASS: units, settings scroll, probe modal, row Run, locked job screen, overrides, completion and Stop", flush=True)
    finally:
        command("TOUCH ON")

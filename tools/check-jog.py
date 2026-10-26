"""Check jog input on the display in UART-disconnected preview mode."""
import importlib.util
from pathlib import Path
import time
import serial
import sys
import json
import os

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = os.environ.get("PANEL_PORT", "COM12")
    port.open()
    time.sleep(2)
    port.reset_input_buffer()
    for attempt in range(15):
        port.write(b"\nUG SYNC\n")
        if b"@UGERR command" in port.readline():
            break
    else:
        raise TimeoutError("Console sync")
    time.sleep(.2)
    port.reset_input_buffer()
    def command(s):
        return remote.debug(port, s)
    def tap(x, y):
        remote.exchange(port, "tap", point=(x, y))
        time.sleep(.3)
    def idle():
        command("RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>")
        time.sleep(.3)
    def shot(name):
        if "--no-captures" not in sys.argv:
            remote.exchange(port, "shot", Path("captures") / name)
    try:
        command("TOUCH OFF")
        command("TEST")
        command("DEMO ON")
        time.sleep(1)
        assert not json.loads(command("PREFS").removeprefix("@UGDATA ")).get("actuator_mode"), "Select CNC mode before this check"
        tap(848, 564)
        tap(761, 448)  # Metric display units
        tap(176, 564)
        tap(631, 184)  # Incremental
        command("RX $110=6000")
        command("RX $111=3000")
        command("RX $112=1200")
        tap(548, 120)  # Fast
        idle()
        tap(578, 268)  # X- Y+
        last = command("LAST")
        assert "$J=G91 G21 X-" in last and " Y" in last and "0x85" not in last, last
        assert "F3000.0" in last, last
        command("RX ok")
        idle()
        tap(694, 358)  # Empty centre must not send a command
        assert command("LAST") == last
        for x, rate in ((638, "F1200.0"), (728, "F300.0")):
            tap(x, 120)
            tap(578, 268)
            assert rate in command("LAST")
            command("RX ok")
            idle()
        tap(938, 290)  # Slow Z uses the Z axis rate, beside XY
        assert "Z" in command("LAST") and "F120.0" in command("LAST")
        command("RX ok")
        idle()
        tap(548, 120)
        tap(885, 184)  # Continuous
        command("HOLD 578 268 700")
        assert "0x85" in command("LAST")
        time.sleep(.5)
        assert "0x85" in command("LAST"), "Jog queued after release"
        idle()
        shot("jog-continuous.png")
        tap(631, 184)
        shot("jog-incremental.png")
        tap(62, 122)  # Home X beside the DRO
        shot("home-x-confirm.png")
        tap(630, 409)
        assert "$HX" in command("LAST")
        command("RX ok")
        idle()
        tap(88, 560)  # Return to Jog and its DRO actions
        tap(192, 346)  # Go to XY work zero, below the DRO
        tap(630, 409)
        assert "$G" in command("LAST")
        idle()
        tap(312, 346)  # Go to Z work zero
        tap(630, 409)
        assert "$G" in command("LAST")
        idle()
        tap(432, 346)  # Set zero all, separate from go-to-zero motion
        tap(630, 409)
        assert "G10 L20 P0 X0 Y0 Z0" in command("LAST")
        command("RX ok")
        idle()
        tap(441, 122)  # Zero X on DRO
        tap(630, 409)
        assert "G10 L20 P0 X0" in command("LAST")
        command("RX ok")
        idle()
        command("RX <Run|SD:42.5,/test.nc|FS:800,12000>")
        time.sleep(.3)
        tap(130, 370)  # Pause on dedicated job page
        assert "0x82" in command("LAST")
        tap(130, 370)
        assert "0x81" in command("LAST")
        tap(384, 564)  # Navigation is locked during the job
        tap(374, 370)  # Job Stop below the DRO
        data = json.loads(command("STATE").removeprefix("@UGDATA "))
        assert data["job"], "Stop skipped confirmation"
        tap(630, 409)
        data = json.loads(command("STATE").removeprefix("@UGDATA "))
        assert not data["job"], "Confirmed Stop left the job running"
        tap(88, 560)
        # A confirmation can be cancelled without issuing motion.
        idle()
        tap(72, 346)  # Home all confirmation below the DRO
        tap(342, 409)  # Cancel without homing
        time.sleep(.5)
        idle()
        tap(578, 268)
        assert "$J=G91 G21 X-" in command("LAST"), "Dialog remained over jog controls"
        command("RX ok")
        idle()
        print("PASS: jog, DRO set/go zero, navigation, homing, pause/resume and Stop", flush=True)
    finally:
        command("TOUCH ON")

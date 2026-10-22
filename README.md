# grblHAL panel

Firmware development for the VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.

Native LVGL grblHAL interface with real hardware screenshot capture and injected
touches over the COM12 USB UART. Controller communication uses UART1, TX37/RX38
at 115200 baud, configured and connected from Settings.

Jog has an eight-direction XY pad, separate Z controls, Incremental/Continuous
modes, Home All and individual axis homing, and XYZ/per-axis work zeroing.
Incremental sends one move for the entered distance. Continuous sends one long
jog and raw `0x85` on release or loss of touch. Distance and feed use numeric inputs.

Controller SD listing/run/progress, pause/resume/reset, alarm recovery and XYZ
corner probing are included. Plate defaults are X10/Y10/Z5 mm; enter tool diameter
on Probe and edit plate defaults in Settings. Preview disconnects the controller
UART and uses synthetic feedback; it is visibly marked on screen.

- [Development instructions and code map](AGENTS.md)
- [Screenshot and touch workflow](docs/device-ui-development.md)
- [UART, RS485 and panel-plugin investigation](docs/hardware-and-protocol.md)

## Build

With ESP-IDF 5.3.4 installed:

```powershell
cd firmware/viewesmart-p4
$env:IDF_TARGET='esp32p4'
idf.py build
idf.py -p COM12 app-flash
```

The checked-in sdkconfig preserves the tested display configuration; do not reset
it just to change targets. On an already configured checkout, use `idf.py build`
directly. Dependency manifests and dependencies.lock identify managed components,
which are downloaded by ESP-IDF and excluded from Git.

## Capture and interact

Use Python with pyserial installed, from the repository root:

```powershell
python tools/ui-remote.py self-test
python tools/ui-remote.py --port COM12 session
```

Then enter `shot captures/home.png`, `tap 650 420`, and
`shot captures/setup.png`. Finish with `quit`.
Disconnect the CNC before automated UI exploration: taps are real UI input.

Run `python tools/check-jog.py` for device jog checks in preview mode. It temporarily
disables physical touch, checks protocol/model cases, diagonals, release-to-stop,
Home X and Zero X, saves framebuffer PNGs, then restores physical touch.
Actual controller wiring and physical motion/probing still require validation.

![Jog pad on the physical display](docs/screenshots/panel-jog.png)

Board source and documentation:
[VIEWESMART hardware](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
Existing component licenses are retained in their source directories.

# grblHAL panel

Firmware development for the VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.

Native LVGL grblHAL interface with real hardware screenshot capture and injected
touches over the COM12 USB UART. Controller communication uses UART1, TX37/RX38
at 115200 baud, configured and connected from Settings.

Jog has an eight-direction XY pad, separate Z controls, Incremental/Continuous
modes, Home All and individual axis homing, and XYZ/per-axis work zeroing.
The landscape layout is intended for two-handed use: XY by the left thumb,
large Z controls by the right, full-width DROs above, and bottom navigation.
Each DRO has an axis-zero button. Individual homing is on Zero / Home.
STOP stays visible on every page and immediately sends Ctrl-X without confirmation;
this resets the controller, so position may need checking or re-homing afterward.
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

## G-code preview

On Files, refresh the controller SD list, select a file, then tap Preview.
The pendant downloads it using `$F<=/path` and reuses uGsender's G-code viewer.
Choose Top or 3D, drag to pan, and use +/− to zoom. Back to files returns to the
list. Run from SD is a separate confirmed action; preview never streams motion.

Downloads are cached in display RAM, with a 2 MiB limit and a 10-second inactivity
timeout. Parsing runs in a background task; Stop remains available over dialogs.
Transfers must finish with `ok`; UART overflow, overlong lines, failed writes and
unsupported previews never produce a successful partial preview. A new status
report is required before further manual commands after downloading.

The reused viewer supports G0/G1 and IJK G2/G3 arcs in G17/G18/G19, absolute/relative
coordinates and mm/inches. Macros, canned cycles, R arcs and offset-changing codes
are rejected for preview; direct controller SD run remains available. This is a
toolpath preview, not a controller execution or collision simulation.

Protocol sources: [Ooznest Control SD handling](https://github.com/Ooznest/ooznest-control/blob/main/js/features/sdcard.js)
and [grblHAL SD commands](https://github.com/grblHAL/Plugin_SD_card).

`python tools/check-preview.py` checks the real display with UART disconnected and
synthetic controller file data, captures both views, and checks the separate run
confirmation. Physical controller download remains unverified.

![Controller SD toolpath preview](docs/screenshots/sd-preview-3d.png)

Board source and documentation:
[VIEWESMART hardware](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
Existing component licenses are retained in their source directories.

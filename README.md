# grblHAL panel

Firmware development for the VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.

Native LVGL grblHAL interface with real hardware screenshot capture and injected
touches over the COM12 USB UART. Controller communication uses UART1, TX37/RX38
at 115200 baud, configured and connected from Settings.

Jog has an eight-direction XY pad, separate Z controls, Incremental/Continuous
modes, Home All and individual axis homing, and XYZ/per-axis work zeroing.
The landscape Jog screen uses compact vertical DRO rows on the left and a grouped
XYZ jog pad on the right, with Z directly beside XY and an empty centre. Each axis
has Home and Zero. Home all, Go to XY zero, Go to Z zero and Set zero all sit
below the DRO; Probe opens a modal and G54-G59 uses a dropdown.
Go to zero moves the machine; Set zero changes the active work offset. Both
require confirmation. Go to XY zero follows Ooznest Control: rapid via machine
Z-5 mm, X0, Y0, then restore original machine Z. Go to Z zero rapids to work Z0.
Generated motion uses mm and restores the original absolute/incremental mode.

Settings uses labelled rows. Units can be mm or inch; positions, probe geometry
and jog distances convert on the LCD. Internal geometry and motion commands stay
in mm. Jog steps are 0.1 / 1 / 10 / 100 mm or 0.01 / 0.1 / 1 / 10 inch.
Continuous sends one long jog and raw `0x85` on release or loss of touch.
Incremental sends one move and finishes its set distance. Fast / Med / Slow use
100% / 50% / 25% of the lowest moving-axis `$110`-`$112` maximum rate, matching
Ooznest Control. The fallback is 1000 mm/min. Units, step and speed are saved.

Workflow: Home -> Jog/probe stock zero -> Files -> Run. Files uses full-width rows
with size, Preview and confirmed Run actions. Running switches to a separate screen
with read-only DROs, percentage, elapsed time, Pause/Resume and Feed/Speed override
sliders (10-200%). Overrides use actual `Ov:` feedback and grblHAL realtime commands.
Jog and settings navigation is hidden during a job. Job Complete appears after fresh
Idle feedback; OK returns to Jog. Stopped or failed jobs never count as complete.
Alarms/errors open a dialog; tap the status message to reopen it.

On Job, Pause/Resume and Stop sit side by side below the DRO. Pause changes to
Resume after hold feedback. Stop returns to its bottom-right position on other
screens and while a dialog is open, so it never obscures dialog actions.

STOP stays visible on every page and above dialogs, and immediately sends Ctrl-X
without confirmation. This resets the controller, so position may need checking
or re-homing afterward. Plate defaults are X10/Y10/Z5 mm; enter tool diameter
in the Probe modal and edit plate defaults in Settings. Preview disconnects the
controller UART and uses synthetic feedback, visibly marked on screen.

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

Run `python tools/check-workflow.py` for units, settings scrolling, probe modal,
SD run, job controls, completion and Stop checks. `tools/check-jog.py` covers
diagonals, release-to-stop, homing and zeroing. Both use UART-disconnected preview
feedback, temporarily disable physical touch and restore it in cleanup.
Actual controller wiring and physical motion/probing still require validation.

![Jog pad on the physical display](docs/screenshots/panel-jog.png)

![Running job on the physical display](docs/screenshots/panel-job.png)

Other device captures: [Files](docs/screenshots/panel-files.png),
[Settings](docs/screenshots/panel-settings.png), [Probe](docs/screenshots/panel-probe.png),
[Job Complete](docs/screenshots/panel-complete.png).

## G-code preview

On Files, refresh the controller SD list and tap Preview in a file row.
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

# Working on grblhal-panel

## Project and hardware

- Repository: https://github.com/petervanderwalt/grblhal-panel
- Board: VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.
- Hardware reference: https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY
- Connected development device: CH340 USB UART on COM12, ESP32-P4 revision 1.3.
- Firmware: `firmware/viewesmart-p4`, based on the user's uGsender firmware.
- Toolchain used: ESP-IDF 5.3.4, LVGL 9.2.2, RGB565, one LCD framebuffer.
- Keep existing third-party copyright and license notices.
- Do not add assistant/product attribution to branch names or commits.
- Use the installed ponytail skill in full mode and RTK for supported commands.
- Do not commit build output, managed dependencies, credentials, or device backups.
- Keep replies short. Do not replace the UI or change controller settings without a task requiring it.

## Build and flash

On this Windows machine, from the firmware directory:

```powershell
$env:IDF_TARGET='esp32p4'
. 'C:\Users\User\Documents\7 Inch Pendant\esp-idf-full\export.ps1'
idf.py build
idf.py -p COM12 app-flash
```

The board supports esptool automatic bootloader entry and RTS reset. Bootloader
offset is 0x2000, application is 0x10000. Use app-flash for application changes.
Do not erase NVS or file storage as part of routine UI development.

The ESP-IDF Python interpreter already has pyserial:
`C:\Users\User\.espressif\python_env\idf5.3_py3.13_env\Scripts\python.exe`.
Use that interpreter if the system Python lacks pyserial.

## Real device screenshots and touches

This is the firmware running on hardware, not a simulator. The display's actual
RGB565 framebuffer is copied and sent over UART0 at 115200 baud; the host utility
reassembles a PNG. No screenshot-specific rendering or image generation is used.

From the repository root:

```powershell
python tools/ui-remote.py self-test
python tools/ui-remote.py --port COM12 shot captures/home.png
python tools/ui-remote.py --port COM12 session
```

In session mode, enter these commands (paths may contain spaces):

```text
demo on
touch off
shot captures/jog.png
tap 848 564
shot captures/settings.png
tap 176 564
touch on
quit
```

`demo on` disconnects the real controller UART before injecting feedback. Always
send it before automated machine-control tests. Wi-Fi remains a real C6 connection.
Use `hold X Y MS` for continuous-jog press/release, `swipe X1 Y1 X2 Y2` for
scrolling/sliders, and `rx <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0>` for
sample feedback. Commands use LCD pixels with origin at the upper left.
Physical two-finger pinch is not supplied by the single-pointer injection device.

For the complete command reference, console protocol, failure handling and
reproduction steps, read [docs/device-ui-development.md](docs/device-ui-development.md).
`tools/capture-gallery.py` is the repeatable page/dialog capture sequence. It
captures into ignored `captures/gallery/`; inspect every PNG before copying it to
`docs/screenshots/`. The README gallery must show this firmware's actual LCD,
not mockups or reconstructed layouts.

Keep one session open for multi-step navigation. Session commands can also be
piped from a text file. Only one serial client may own COM12. The host probes the
console before sending commands because boot/open timing can vary. It fails a
capture if the device resets, rows are missing, RLE is invalid, or CRC32 differs.

Inspect every resulting PNG before claiming a UI task complete. Test at least a
navigation round trip. A capture takes seconds and depends on image complexity;
do not treat this as video.

Taps invoke real LVGL events and may activate machine controls. Automated checks
must use UART-disconnected preview. Before live controller tests outside preview,
confirm machine isolation; do not assume an earlier disconnection still applies.

## Code to retain when porting this workflow

- `firmware/viewesmart-p4/main/ui_remote.c`: `ui_remote_start()` installs
  UART0 RX and the command task; `screenshot()` encodes pixels; `tap_tick()`
  dispatches press/release on the LVGL task.
- `firmware/viewesmart-p4/main/ui_remote.h`: public startup declaration.
- `firmware/viewesmart-p4/main/main.c`: starts the remote console after display/UI
  initialization and after releasing the display lock.
- `firmware/viewesmart-p4/main/CMakeLists.txt`: includes `ui_remote.c`.
- `firmware/viewesmart-p4/components/esp32_p4_ultra/esp32_p4_ultra.c`:
  retains the DPI panel handle and implements `bsp_display_copy_framebuffer()`.
- `firmware/viewesmart-p4/components/esp32_p4_ultra/include/bsp/display.h`:
  declares the capture helper.
- `tools/ui-remote.py`: serial client, row decoder, RGB565-to-PNG conversion,
  checksum validation, and a dependency-free decoder/PNG self-test.

**Hardware finding:** copying PSRAM directly to PSRAM caused repeatable watchdog
resets on this device. Retain the internal-RAM row staging in
`bsp_display_copy_framebuffer()`. Copy a row into internal RAM, then into the
PSRAM snapshot. Do not replace it with a full-frame memcpy. Keep rendering and
touch callbacks on the normal LVGL task; do not force a refresh from the UART task.
Capture currently supports one RGB565 framebuffer; other configurations return
an error. Call the framebuffer helper while holding the BSP display lock.

Wire protocol: newline-delimited `UG SHOT` and `UG TAP x y`. Responses:
`@UGSHOT width height crc32`, ordered `@UGROW y rle`, `@UGEND`,
`@UGOK TAP`, or `@UGERR reason`. Each RLE run is four hexadecimal digits
of count followed by four of RGB565 pixel value. CRC32 covers the complete
little-endian RGB565 image.

## Current controller and UI code

- `main/panel_model.cpp/.h`: portable protocol parser, command queue, jog/zero/home,
  SD jobs, faults and acknowledged corner-probe sequence.
- `main/panel_runtime.cpp/.h`: UART1 worker, mutex and explicit UART-off preview.
- `main/panel_ui.cpp/.h`: LVGL pages, jog press/release handling, numeric inputs,
  NVS preferences and confirmations.
- `main/tests/panel_model_test.cpp`: embedded protocol tests (`UG TEST`).
- `tools/check-jog.py`: physical display checks using preview feedback and injected touches.
- `tools/check-preview.py`: controller SD download/view/run checks using synthetic file contents.
- `main/gcode_viewer.cpp`: reused uGsender renderer, memory parsing and view controls.

SD preview uses `$F<=/path`, never `$F=/path` until a separate confirmed Run.
Keep the 2 MiB PSRAM cap, acknowledgement requirement, inactivity timeout and
overflow invalidation. File content must be consumed by the download sink before
normal report parsing. Do not interpret G-code file content as controller reports.
Filter realtime characters from filenames and honour `UNUSABLE` file entries.
Background parsing holds the RAM cache against replacement. Never attach or render
the canvas while the parser task is editing its vertices. Preview rejects unsupported
codes rather than displaying truncated geometry. Preserve responsive Stop during parsing.

Design for landscape, two-handed pendant use. On Jog, keep compact vertical DRO
rows on the left and group the entire XYZ pad on the right, with Z immediately
beside XY and a blank centre. Use Jog / Files / Settings navigation.
Jog work coordinates use 28 px type and DRO rows have no coloured borders.
Do not show protocol command bytes or a redundant units/WCS footer to end users.
The top-right header is a Wi-Fi shortcut with SSID/IP or Not connected; do not
duplicate the active tab name there. Signal brightness uses the C6 RSSI reading.
Jog / Files / Settings tab accents are purple / green / orange, with opaque
backgrounds and a solid selected fill; no top selection line. Use subtle button
gradients and card shadows. Fault dialogs have square edges and red title bands; successful
completion uses green. Include simulated alarms and errors in workflow checks.
Axis jog uses X red / Y green / Z blue; XY diagonals use teal. Home and Zero
use quiet flat backgrounds with subtle axis-coloured outlines; DRO action buttons
are quiet too. Keep the jog pad and orange Probe action prominent.
The Settings section is labelled Probe, without Defaults.
Jog DRO and jog-pad cards are both 440 px high. There is no small footer status
label. Notices use dialogs; the header alarm/error status reopens fault dialogs.
Disabled dropdowns use solid light backgrounds and muted dark text and chevrons.
Disabled XY/diagonal arrow strokes use the same muted dark colour as Z button text.
File preview controls and filename are inset inside the card. Keep Top/3D and
physical pinch zoom, without +/- buttons or successful download/parse notices.
The file-list interior is white; the main grey background separates the cards.
Wi-Fi Scan/Disconnect sit below the title band; Close is in its top-right corner.
All modal frames have square corners. Probe and Wi-Fi scan share the same white
header close icon with a transparent background. Keep touch buttons rounded.
Wi-Fi lists use white, borderless rows; the whole network row remains tappable.
Scan rows show signal strength followed by a lock icon for secured networks; open
networks have no lock. The Font Awesome glyph and license are in main/assets/.
Files and Settings have no DRO. Probe is a modal launched from Jog; remove Probe and Zero /
Home tabs. G54-G59 uses a dropdown on Jog. Keep labels short; omit instructional
filler such as "tap once" and "move the set distance". Settings uses labelled rows.

The controller has physical E-stop buttons. There is no global on-screen STOP.
Job Stop sends Ctrl-X on touch-down without confirmation, below the DROs.
Keep confirmations for homing, zeroing, probing and starting jobs.
Keep per-axis Home/Zero on each Jog DRO, with Home all, Go to XY zero, Go to Z zero
and Set zero all directly below. Set zero changes offsets; Go to zero moves.
Go XY follows `DROHandler.goXY0()`: machine Z-5 mm, X0, Y0, restore machine Z.
Generated motion uses mm, restores distance mode and waits for fresh Idle before
further manual motion. Never generate G20 to match LCD units or change `$13`.

Convert mm/inch only on the LCD; geometry and runtime commands remain mm.
Jog steps: 0.1, 1, 10, 100 mm or 0.01, 0.1, 1, 10 inch (0.254, 2.54, 25.4, 254 mm).
Use actual directional arrows. Continuous cancels on release/lost press;
incremental finishes its distance. Fast / Med / Slow use 100% / 50% / 25% of the
minimum maximum rate among moving axes (`$110`-`$112`), fallback 1000 mm/min.
Legacy Preferences `step` and `jog_feed` preserve NVS blob compatibility. Separate
keys are `jogspeed`, `stepidx` and `inches`.

Workflow: home -> jog/probe stock zero -> select/run controller SD file -> locked
Job screen -> Job Complete -> OK -> Jog. Job has read-only DRO, percentage, elapsed
time, Pause/Resume, Feed/Speed sliders and Stop. Hide navigation during jobs.
Completion needs fresh Idle; Program End alone is insufficient. Stopped or failed
jobs never count as complete. Files has full-width rows with size, Preview and Run.
Alarms/errors use dialogs, with no persistent Recover button. Disabled controls
must remain readable: muted dark text/arrows on a solid light background. Preserve probe
contact, geometry and spindle interlocks.

On Job, Pause/Resume and Stop sit side by side below the DRO. Pause changes to
Resume after hold feedback. Dialogs cover these controls and have their own close
or Cancel action. Jog / Files / Settings use the native LVGL bottom tabview,
with no content swiping; hide it during jobs and until completion is acknowledged.

`tools/check-workflow.py` checks LCD units, settings swipes, the Probe modal,
row Run, navigation lock, overrides and outcomes with UART1 disconnected.
Update injected-touch coordinates when moving controls.

The inherited wizard/link files remain as reference but are not compiled by
`main/CMakeLists.txt`. `main.c` creates the panel UI.

Continuous jog starts on `LV_EVENT_PRESSED` and cancels with raw `0x85` on
`LV_EVENT_RELEASED`/`LV_EVENT_PRESS_LOST` and page rebuild. Incremental sends once
on press and is not cancelled on release. Cancel clears an unsent queued jog;
manual motion waits for fresh Idle feedback before another command.

Protocol reference: Ooznest Control source at commit
`7ce3f90206f1428ae1959136ba5bbdb439f00d1c`, especially
`js/machine/jogging.js`, `js/machine/dro.js`, `js/ui/ui.js`,
`js/features/sdcard.js` and `js/machine/probe.js`.
Override reference: grblHAL core commit `4ec1ae44b857a70d2f9298344feac4b43ed0eb6b`,
`grbl.h` and `protocol.c`; Ooznest `index.html` and `js/machine/dro.js`.
Feed coarse/fine +/-: 0x91/0x92 and 0x93/0x94. Spindle: 0x9A/0x9B and 0x9C/0x9D.
Read `Ov:feed,rapid,spindle`; wait for feedback before another slider target.
Do not rewrite controller override settings.

For builds newer than 20261013, use one startup `0x87`, then poll `0x80`.
Do not change `$10` automatically. SD jobs use `$F+` and `$F=/path` on the controller.

## Controller transport

Keep full grblHAL serial support as a first-class option. The user has UART pins
on the controller and proposed display IO37 TX / IO38 RX. Cross TX to controller
RX and RX to controller TX, with common ground and verified 3.3V UART levels.

The active panel defaults use UART1 TX37/RX38. The inherited link used TX27/RX26.
The manufacturer's RS485 example uses those pins and LDO4 at 3300mV. Do not claim
that the proposed IO37/38 path or RS485 bus has been hardware-tested yet.
Verify board revision, pin availability and wiring before changing the default.

Plugin reference: https://github.com/dresco/Plugin_panel
It offers Modbus RTU and CAN panel interfaces; it is distinct from the full
grblHAL sender protocol over UART. The controller initiates Modbus requests, so the display must serve its
register map as a Modbus RTU server. Investigate the full map before implementing
RS485 transactions. Keep controller-side plugin changes separate from display
firmware. Do not send motion commands or auto-change controller settings during
communication discovery.

See `docs/device-ui-development.md` for transport details and
`docs/hardware-and-protocol.md` for the communication investigation.

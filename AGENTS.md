# Working on grblhal-panel

## Project and hardware

- Repository: https://github.com/petervanderwalt/grblhal-panel
- Board: VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.
- Hardware reference: https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY
- Connected development device: USB UART on COM12 (verify the current port), ESP32-P4 revision 1.3.
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

`demo on` disconnects both real controller transports before injecting feedback. Always
send it before automated machine-control tests. Wi-Fi remains a real C6 connection.
Use `hold X Y MS` for continuous-jog press/release, `swipe X1 Y1 X2 Y2` for
scrolling/sliders, and `rx <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0>` for
sample feedback. Commands use LCD pixels with origin at the upper left.
Physical two-finger pinch is not supplied by the single-pointer injection device.

Wi-Fi credentials persist in the `panelwifi` NVS namespace after obtaining an IP.
Startup reconnects automatically; transient disconnects retry once per second
within the 30-second connection window. Explicit Disconnect stops retries.
`tools/check-wifi-reboot.py` checks three RTS reboots using saved credentials;
it restores UART-disconnected demo mode and physical touch afterward.

For the complete command reference, console protocol, failure handling and
reproduction steps, read [docs/device-ui-development.md](docs/device-ui-development.md).
`tools/check-theme-gallery.py` captures current Jog states, both SD sources and
cached previews into ignored `captures/readme-refresh/`. Use
`tools/check-fullscreen.py` for secondary pages in `captures/fullscreen/`.
Inspect every PNG before copying it to
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
Row-sized ROM memcpy calls also produced intermittent capture-time corruption on
this display. Keep the explicit volatile byte loops in both staging passes.
The capture phase markers and `UG STATE` reset reason help diagnose a recurrence.
Keep the staging row and RLE buffer in internal RAM. The snapshot is cache-line
aligned in PSRAM and written back before releasing the display lock.
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
- `main/panel_runtime.cpp/.h`: UART1/USB worker, mutex and isolated preview.
- `main/panel_ui.cpp/.h`: LVGL pages, jog press/release handling, numeric inputs,
  NVS preferences and confirmations.
- `main/tests/panel_model_test.cpp`: embedded protocol tests (`UG TEST`).
- `tools/check-jog.py`: physical display checks using preview feedback and injected touches.
- `tools/check-preview.py`: cached display-SD preview checks on the development card.
- `main/gcode_viewer.cpp`: reused uGsender renderer, memory parsing and view controls.

Display SD uses cached native bitmap previews generated by the PC service. Controller SD
has listing and Run only; its inherited download/viewer code is not exposed.
Filter realtime characters from filenames and honour `UNUSABLE` entries.

Design for landscape, two-handed pendant use. On Jog, keep compact vertical DRO
rows on the left and group the entire XYZ pad on the right, with Z immediately
beside XY and a blank centre. Use Jog / Files / Macros / Diag / Settings navigation.
Jog work coordinates use 28 px type and DRO rows have no coloured borders.
Do not show protocol command bytes or a redundant units/WCS footer to end users.
The top-right header is a Wi-Fi shortcut with SSID/IP or Not connected; do not
duplicate the active tab name there. Signal brightness uses the C6 RSSI reading.
Jog / Files / Macros / Diag / Settings tab accents are blue / green / teal / orange, with opaque
backgrounds and a solid selected fill; no top selection line. Tab taps slide the
shared page_layer horizontally for 220 ms; keep the header and tab bar stationary.
The animation layer holds controls only during the slide; restore their root
parent afterward. Page rebuilds cancel slides and restore x=0. Mode and speed
selection update in place; only change matrix disabled flags when readiness changes.
Command-busy locks input immediately but keeps setup colours steady for the first
200 ms while Idle; offline, faults, Hold and sustained busy states use pale disabled styling.
Speed/mode selectors retain their checked palette throughout Idle/Jog while input
is unavailable; they still grey out for disconnect, Hold, alarms and errors.
Use subtle button
gradients and card shadows. Fault pages use red accents on the full-screen graphite/card layout; successful
completion uses green. Include simulated alarms and errors in workflow checks.
Axis jog uses X red / Y green / Z blue; XY diagonals use teal. Home and Zero
use quiet flat backgrounds with subtle axis-coloured outlines; DRO action buttons
are quiet too. Keep the jog pad and orange Probe action prominent.
Neutral buttons and dropdowns use light fills and dark text. Jog speed and mode
use native one-checked button matrices with pale blue selection styling.
Probe and jog selectors are disabled and pale whenever machine setup is unavailable.
Set per-item DISABLED controls on button matrices as well as the widget state.
The Settings section is labelled Probe, without Defaults.
Jog DRO and jog-pad cards are both 440 px high. There is no small footer status
label. Notices use dialogs; the header alarm/error status reopens fault dialogs.
Disabled dropdowns use solid light backgrounds and muted dark text and chevrons.
Disabled XY/diagonal arrow strokes use the same muted dark colour as Z button text.
File preview controls and filename are inset inside the card. Use the eight
cached views and physical pinch zoom, without +/- buttons or success notices.
The file-list interior is white; the main grey background separates the cards.
Wi-Fi Scan/Disconnect sit below the title band; Close is in its top-right corner.
Secondary screens fill the LCD with graphite, a title/close control above a rounded
black content card, and rounded dark controls. No floating or translucent overlays.
Probe Configuration uses separate rounded settings and numeric-keypad cards, matching Jog.
Wi-Fi lists use white, borderless rows; the whole network row remains tappable.
Scan rows show signal strength followed by a lock icon for secured networks; open
networks have no lock. The Font Awesome glyph and license are in main/assets/.
Files and Settings have no DRO. Probe is a full-screen page launched from Jog; remove Probe and Zero /
Home tabs. G54-G59 uses a dropdown on Jog. Keep labels short; omit instructional
filler such as "tap once" and "move the set distance". Settings uses labelled rows.

The controller has physical E-stop buttons. There is no global on-screen STOP.
Job Stop opens a confirmation below the DROs; only Confirm sends Ctrl-X.
Keep confirmations for homing, zeroing, probing and starting jobs.
Keep per-axis Home/Zero on each Jog DRO, with Home all, Go to XY zero, Go to Z zero
and Set zero all directly below. Set zero changes offsets; Go to zero moves.
Go XY follows `DROHandler.goXY0()`: machine Z-5 mm, X0, Y0, restore machine Z.
Generated motion uses mm, restores distance mode and waits for fresh Idle before
further manual motion. Never generate G20 to match LCD units or change `$13`.

Convert mm/inch only on the LCD; geometry and runtime commands remain mm.
Jog steps: 0.1, 1, 10, 100 mm or 0.01, 0.1, 1, 10 inch (0.254, 2.54, 25.4, 254 mm).
Use actual directional arrows. Continuous cancels on release/lost press;
incremental finishes its distance. Fast / Med / Slow use 100% / 40% / 10% of the
minimum controller-reported maximum rate among moving axes (`$110`-`$112`).
There is no fallback rate: every moving axis must have a valid report. Jog direction
buttons stay disabled until their required rates arrive; include known-rate axes
in the Jog appearance key. New reports affect the next jog without rebooting.
Legacy Preferences `step` and `jog_feed` preserve NVS blob compatibility. Separate
keys are `jogspeed`, `stepidx` and `inches`.

Workflow: home -> jog/probe stock zero -> select/run display or controller SD file -> locked
Job screen -> Job Complete -> OK -> Jog. Job has read-only DRO, percentage, elapsed
time, Pause/Resume, Feed/Speed sliders and Stop. Hide navigation during jobs.
Completion needs fresh Idle; Program End alone is insufficient. Stopped or failed
jobs never count as complete. Files has paged rows with size and Run; only Display SD offers Preview.
Alarms/errors use dialogs, with no persistent Recover button. Disabled controls
must remain readable: muted dark text/arrows on a solid light background. Preserve probe
contact, geometry and spindle interlocks.

On Job, Pause/Resume and Stop sit side by side below the DRO. Pause changes to
Resume after hold feedback. Dialogs cover these controls and have their own close
or Cancel action. Jog / Files / Macros / Diag / Settings use the native LVGL bottom tabview,
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

## Actuator mode

Settings Mode selects CNC or Actuator and persists separately from the existing
preferences blob. Actuator mode hides Files and replaces Jog with four XYZA
actuator cards. Configuration and editable labels belong in Settings only.
Use four full-height columns across the screen, with large vertical linear
sliders. Position tracks are uniformly coloured on both sides of the thumb.
`panel_actuators.h` holds the versioned settings; `panel_actuators_ui.inc` reuses
the private UI helpers. Slider moves commit only on release. Never queue moves
while dragging or automatically zero work coordinates after homing.

Require controller-reported homing, known machine coordinates, endpoint bounds
and fresh Idle feedback. Use G53 metric absolute jogs, not work-coordinate moves.
Do not assume A exists or is configured as linear. The first version permits
one actuator move at a time. Stop cancels the active jog without controller reset.
Only A offers Linear/Rotary selection (`arotary` NVS key). Require a matching
controller `$376` report before A moves; its bit zero selects A. Explicitly saving
A's settings while connected/idle may change that bit, preserving every other
rotary bit, then query `$$` for readback. No startup/open-dialog type writes.
Offline edits are local until Save is used while connected. Rotary A uses degrees and deg/min in every LCD unit mode;
the parser exempts rotary axes from `$13` inch conversion. Position layout uses
an angular dial, with one move on release and bounded endpoints, without wrapping.
`tools/check-actuator-rotary.py --port COM17 --configure-demo` writes a Pan
example, checks the dial, type mismatch and inch display, and captures the LCD.
Repeat layout runs one actuator between bounded endpoints until Stop. Keep its
state machine in the model, with fresh Idle position at each reversal. Speed
changes cancel and restart the same leg only after fresh Idle position; zero
disarms. Navigation away also disarms. Never persist an armed repeat state.
`tools/check-actuator-repeat.py --port COM17 --configure-demo` selects Repeat
for Z and checks reversals, speed changes, Stop and navigation cancellation.
The Wi-Fi actuator page is read-only; remote commands and MQTT are future work.
See `docs/actuator-mode.md` for protocol details, research and development hooks.
`tools/check-actuators.py --port COM17 --configure-demo` writes development
example settings and checks the new UI; use only on a development display.
The existing CNC checks and gallery require CNC mode and retain their own
sequences. Do not add actuator configurations to the CNC gallery implicitly.

The display was identified on COM17 on 2026-10-23 using `UG PREFS`; enumerate
ports and identify firmware before resetting or flashing. The CNC check scripts
accept `PANEL_PORT` to override COM12.

## Inputs, accessories and macros

Diagnostics is read-only and works in both modes. Preserve the distinction
between `Pn:` control/limit snapshots and extended I/J/K probe deltas. Missing
or stale feedback must not look active or safely released. Never enable `$10`
automatically. The Jog Spindle / coolant button opens manual controls; use
controller `$30`/`$31` limits and explicit spindle-start confirmation. `A:` is
reported accessory state, not a prediction from a click. Retain spindle-direction
interlocks for probing and actuators, including controllers without `FS:` RPM.

CNC tab order is Jog / Files / Macros / Diag / Settings. Internal page IDs remain
Jog=0, Files=1, Settings=2, Macros=3; map native tab indices explicitly. Actuator
mode hides Files and Macros. `panel_tools_ui.inc` and `panel_macros.h` contain
the screens and bounded NVS store. Macros use the acknowledged model queue,
validate the whole batch before sending, and remain busy until fresh Idle.
Keep navigation on Macros during execution so confirmed Stop remains accessible.
`UG MACRO FIELD <0|1> <ASCII-hex>` fills an open name/code editor only; it never
saves or runs. See `docs/inputs-accessories-macros.md` and `tools/check-tools.py`.
The macro grid is four columns by five rows; Add is in the header and Delete
is in the editor. Store the twenty entries in PSRAM, with one NVS key per macro.
`tools/check-macro-grid.py` verifies all twenty maximum-length entries survive a
reboot. Set new LVGL keyboards to TOP_LEFT before positioning: they default to
BOTTOM_MID and otherwise disappear below the modal.
When saving a button-matrix choice, read its CHECKED control flags; selected-button
is the last clicked item and may be NONE when the initial choice was untouched.

## Controller transport

The Machine settings browser uses controller metadata, not a fixed settings list.
LVGL uses `CONFIG_LV_USE_CLIB_MALLOC=y` so widget allocations use ESP-IDF's heap
instead of a fixed 64 KiB pool. Retain this when porting the grouped browser.
Keep LCD DMA buffers in internal RAM.
Keep `$EG`, `$ES`, `$ESH`, `$$` acknowledgement-paced. `$ES` fields 8/9 are
reboot/allow-null flags; current values come from `$$`. Save only an explicitly
edited setting while idle, then read back and verify. Never write on startup or
when opening a dialog. Keep passwords masked and setting-write values out of LAST.
`UG SETTING FIELD <ASCII-hex>` fills an open editor/search field without saving.
See `docs/machine-settings.md` for implementation and limits.

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

Spindle selection in the accessory popup reads `$SPINDLESH`; `[SPINDLE:id|number|type|caps|name|min,max]` supplies the command number, active `*`, and RPM range. Disabled `-` numbers cannot be selected. Confirmed selection sends `M104Q<number>` then reads back the list; never treat hardware ID as the command number or activate from discovery alone. `tools/check-spindles.py` verifies the real LCD in UART-off demo mode.

## Display SD, character-counting sender and PNG previews

The default file location is Onboard SD; Settings can select Controller SD with
listing and Run only. `panel_storage.cpp` mounts the inserted card without
formatting, scans up to 512 jobs into a PSRAM catalogue, and exposes four rows
per page. Initialise each sort index before publishing a scan entry; the LVGL
snapshot may run during scanning. Never render hundreds of LVGL rows at once.
Cache files belong only in `.panel-previews/v4-<sha256>/`; never rewrite original
G-code. Shared SDMMC cleanup must release slot 0 only, preserving C6 slot 1.
Long FAT filenames are enabled in sdkconfig. `panel_fs.cpp` validates native
bitmap headers and reads through a 1 KiB internal staging buffer into a single
RAM canvas. Keep SD DMA away from PSRAM image memory. The server also keeps PNG
exports; the pendant does not decode PNGs.


`panel_stream.cpp` is the independent file sender. It validates the entire file
before motion in bounded batches and reads buffered chunks, not per-byte stdio.
Its FIFO counts exact ASCII wire bytes including LF and stays below 128 bytes,
following Ooznest `js/machine/webserial.js`. Only acknowledgements free FIFO
space. Realtime polls and controls bypass it. Never route a job through the
small macro queue. EOF and all acknowledgements still require a later fresh
Idle report before completion. Faults/SD errors/reset/disconnect cancel the
sender; failed jobs require confirmed Stop/reset. No automatic restart.

The model lives in PSRAM to leave internal RAM for DMA, network tasks and the
screenshot staging buffers. Keep snapshots on the runtime mutex; raw State
copies use the same explicit byte-loop precaution as capture on this P4.
`UG SD STATE` reports the card, current page, server and cache readiness.
`UG PREVIEW SERVER <url>` fills the open Settings field without saving.
`tools/check-display-sd.py` uses actual card files with UART-off demo responses.

`preview-server/server.py` accepts hash-addressed raw uploads and generates the
eight 740x328 PNGs and native RGB565 bitmaps using `renderer.py`. It checks the upload hash, serialises
rendering work and publishes a ready/error manifest. The pendant prepares
previews only through Files > Update previews, while idle, and caches every native
view before writing its completion marker. No generation or content hashing on
page entry, Wi-Fi reconnect or preview selection. Small v4-path indexes restore
cache mappings by path, size and timestamp after reboot; explicit Update hashes
content. Keep HTTP and preparation work off LVGL.
`tools/check-preview-update.py` checks idle browsing, explicit start and Cancel.
The service binds localhost by default; LAN use needs an explicit bind address.
Do not add or broaden host firewall rules without user approval. See
`docs/display-sd-and-previews.md` for limits, setup and validation.

The tracked `patches/patch-hosted.py` runs after CMake dependency resolution.
It stages Hosted SDIO payloads through 64-byte-aligned internal 512-byte RX/TX buffers because
IDF 5.3 rejects PSRAM buffers for block transfers. Keep the Hosted pool in PSRAM;
an internal-only pool prevents this firmware from allocating its startup tasks.
Do not remove the staging or replace its explicit byte copies on this P4.

The idle stack is 4096 bytes; the inherited 1536 bytes overflowed during SD/Wi-Fi and capture checks.

Native previews reuse one 485,440-byte canvas for every view; pan/zoom performs
no SD reads. The SD VFS permits five open files.

`panel_memory.c` wraps memcpy/memmove whenever either buffer is in PSRAM,
staging through 128 internal bytes. Retain its CMake linker flags and embedded
copy/overlap self-test when porting this early-P4 workaround.

The gCAM HTTP handoff and Files page share Settings > File location, persisted
as panelstorage/gcam_local in NVS. Files has no source dropdown. Local saves use exclusive
creation (never overwrite), refresh the catalogue, and never auto-run. Controller SD
keeps its acknowledged transfer. `tools/check-handoff-sd.py` checks real local saves
and both route choices in UART-off demo mode. Decoded handoff limit remains 2 MiB.

Preview text fallback reads the first 14 lines (4 KiB maximum, 80 displayed
characters per line) through panel_file_head.h. Keep it read-only and bounded;
never send file contents just to preview them. tools/test-file-head.cpp checks
CRLF, blank/long lines, limits, controls and small buffers. Preview uses a top-left
back arrow (LCD 56,112), not the old top-right Back to files button.

Single-surface probing is in panel_probe_ui.inc and Model::probe_surface. Z is
initially selected; it uses plate thickness only, never tool radius. X/Y use
minus direction * (plate offset + radius), following Ooznest runSingleAxis.
The existing quick touch/release gate auto-arms on opening Probe. Rebuilding
that modal must not reset a contact check already waiting for release. Do not
add a protection wizard duplicating grblHAL. ProbeSettings layout is persisted;
do not add transient mode fields to it. UI check: tools/check-probe-surfaces.py.

Colour roles are documented in docs/color-scheme.md: PRIMARY is clear blue,
positive actions green, cancel red outline, destructive actions red, warnings
amber with dark text. No purple/lavender accents. Do not tint normal headers
success green. tools/check-colors.py checks contrast and retired literals.
Brightness is 10-100%, live while sliding, saved on release as panel/backlight.
The WS2812B is GPIO30 (D4/R60 in the schematic), driven through RMT without an
extra dependency. State policy is in panel_hardware.h; device checks use
PREFS brightness/rgb_ready/rgb_color and tools/check-hardware-ui.py.

Screenshot checksums run in 16 KiB chunks with a task yield between chunks.
Do not put full-heap integrity walks back into the screenshot hot path: the
capture watchdog mitigation keeps large PSRAM walks and uninterrupted checksum work
out of this path.
Keep the existing row staging, bounded cache maintenance and host CRC validation.

Shared choice matrices use CLICK_TRIG so VALUE_CHANGED runs after LVGL updates
the checked item. Keep callbacks on release; running refresh during press left
Continuous active while Incremental remained highlighted. PREFS exposes
continuous/mode_checked for the real-touch check and scroll_y for capture
coordinates after Settings rows change.

Settings actions (Preview server, Input diagnostics, Machine settings) belong in
scrollable label/button rows, not the title area. Probe Configuration has one Edit row
opening a full-screen XYZ/feed/inset form and shared numeric keypad, with one
decimal in mm and three in inches. Validate all five fields before saving. Never
round saved geometry on open or Cancel. Dialogs use an opaque full-screen backing without dimming or floating shadows; preserve
background input blocking and fully restore the page on close. Keep the internal
LCD drawing buffer; do not add PSRAM rendering experiments on this early P4. See tools/check-settings-rows.py and the dialog diagnostics in
UG PREFS before changing popup rendering.

Full-screen editors (Macro Add/Edit and Preview Server)
use solid red Cancel/Delete on the left and mint Save on the right. Keep these
roles consistent in both concept_macro_dialog and concept_form_dialog. Settings
uses flat opaque list rows with bottom dividers; do not restore per-row shadows
or rounded card clipping. Probe Configuration groups XYZ, feed and corner inset
in one editor and validates all five before saving. Tool diameter stays on Probe.

Settings uses the same graphite background, black list and dark neutral controls
as the refreshed main screens. Keep rows flat, with simple bottom separators.
When restoring source backups, touch their timestamps or clean the build; a newer
object can otherwise retain reverted PSRAM buffer experiments.

Settings keeps its heading above the rounded outer card, matching Files/Macros.
The flat scrollable list is inset inside that card; keep the heading stationary
while scrolling and include it in the normal page slide.

The main CNC dock is Jog / Files / Macros / Diag / Settings. Preserve
page IDs Jog=0, Files=1, Settings=2, Macros=3, Job=4, Complete=5; Diagnostics=6.
Use tab_pages/tab_for_page instead of page-number comparisons for navigation.
CNC dock centres are x=116/314/512/710/908 at y=566. Actuator mode hides Files
and Macros but keeps Diagnostics. Input diagnostics lives on this read-only
page, not a Settings row or modal. Retain unknown/stale and pin-reporting states.

The shared status/Wi-Fi card is a native rounded rectangle at (502,-36),
506x96 with radius 28. Its upper corners sit off-screen so it joins the bezel.
Keep its bottom edge at y=60; the status and Wi-Fi contents sit 8 px above their
original positions. The bottom dock is (16,540), 992x96 with radius 26 and 49 px
bottom padding, extending off-screen while its pills remain at y=545, height 42;
use normal screen clipping, without a canvas or custom raster drawing.

Full-screen form implementation: panel_fullscreen.inc styles secondary controls;
open_modal now builds a full 1024x600 page with a rounded (16,76,992,508) content
card. The internal name remains for existing return callbacks. finish_modal runs
before drawing each page, including dynamic Wi-Fi and machine-setting lists.
Keep validation/confirmation/Cancel behavior intact. Probe Configuration now uses
the shared graphite backdrop with separate settings/keypad cards. New keyboard function keys
use subdued charcoal, not cyan. Update/OK and Cancel retain semantic colours.
UG PREFS exposes form_kind and actual form_w/form_h for reliable checks. Use
check-fullscreen.py in UART-off demo mode; it never confirms motion or saves
machine settings/macros. PANEL_TEST_WIFI_PASSWORD optionally reconnects only
to the approved Braai-Fi network; never commit or print its value.

The screenshot RLE packet buffer is reserved statically in internal RAM (8240
bytes) so dense settings pages cannot fragment away its allocation. Retain the
existing framebuffer staging and volatile byte-copy workarounds unchanged.
Full-screen pages poll only their visible controls; do not repeatedly restyle
whole lists from enable() or redraw hidden Jog controls behind an opaque page.

USB3 CDC ACM host uses peripheral_map bit 1 and GPIO24/25, routed with usb_serial_jtag_ll_phy_select(1). Reserve the USB host/class tasks before UI widgets; keep CDC buffers at 256 bytes and usb_host_cdc_acm 2.4.1 for safe allocation-failure cleanup. Demo mode disables both controller transports. UG USB STATE reports enumeration and traffic. RP2350 2E8A:000A identification, feedback and reconnect were verified; motion remains untested. Normal boot locks unlock with $X, as in Ooznest performUnlock; do not reset or home automatically.

The selected controller transport connects on pendant boot. Explicit Disconnect suppresses retries until Connect or reboot; demo mode remains fully isolated. USB re-enumeration and identification timeout retry only identification, never interrupted jobs/macros. A persistent USB open failure with a physically present device cycles root-port power through the SDK, without changing the controller's supply or settings. tools/check-controller-boot.py checks startup and can watch a physical controller reset; capture complete UART output for any LCD restart.

CNC Jog caches control appearance by availability/mode flags. Idle/Jog updates change DRO, status and Wi-Fi data only; do not restyle or grey all controls around each jog. The model still rejects busy/non-idle commands; busy direction taps are ignored without queuing work or opening a popup. Alarms, disconnects, jobs and other machine modes still disable controls. Invalidate the appearance key on page rebuild and faults. UG PREFS exposes jog_speed, jog_hold_ms, jog_control_updates and native jog_enabled. tools/check-jog-response.py checks continuous/incremental stability, release timing, busy rejection and fault/disconnect disabling in demo mode; it leaves demo isolated. Zeroing confirmation has a neutral title/card and Cancel with a green Set zero action; USER_2 marks its explicitly neutral Cancel so the full-screen styling does not force it red. Fault dialogs retain their red styling.

Machine Settings uses the standard full-screen page with a 24 px content inset: toolbar at (40,100), lists at y=164 and footer at y=512. Reserve a scrollbar gutter beside setting values; keep these explicit browser coordinates instead of relying on legacy modal-size insetting.

Probe Configuration keeps five labelled 64 px fields on the left and a large numeric keypad on the right. Save/Cancel sit below the settings; use the common header/close control. Probe feed, thickness and inset entry points open this same editor and focus the relevant field. Preserve validation, metric/inch conversion, precise unchanged values and return-to-Probe behaviour.

Controller TTL UART pins are fixed TX37/RX38 in panel_runtime.h; only baud rate is editable. Retain legacy preference fields for NVS compatibility and ignore saved pin overrides. Tool Diameter and UART baud use the two-card numeric layout with Cancel/Apply below the input and a right-hand keypad.

# Device UI development

## Measuring page speed

`python tools/benchmark-pages.py --samples 10 --warm-settings --output captures/page-perf/run.json`
measures the five CNC tabs and secondary forms on the real display. It opens and
cancels forms, restores physical touch, and checks that saved preferences stay
unchanged. Use an idle, isolated controller; it reads settings metadata but sends
no motion commands. Keep the same SD contents and controller connection for
before/after runs. Wi-Fi must settle before measurement.

`python tools/ui-remote.py perf` (or `perf` in a session) reads the last completed
sample through `UG PERF`. Firmware timestamps separate construction phases,
layout, software drawing, flush callbacks and flush waits. `transition_us` runs
from construction start to the first completed LVGL refresh; it is not a measured
LCD scanout time. Drawing includes flush/wait time, so do not add those columns
together. Host command time includes UART and injected-touch overhead and is not
the page latency. A screenshot is taken separately, outside the timed run.

The report for the current hardware is [Page benchmarks](page-benchmarks.md).

The firmware in `firmware/viewesmart-p4` exposes screenshot capture, injected touch
and preview-feedback commands on its UART0 console (COM12, 115200 baud). Captures
come from the physical LCD framebuffer, including custom drawing. The same native
LVGL UI handles both finger input and injected input.

Close other serial monitors before using the utility. Install Python's pyserial
package if needed; the local ESP-IDF Python environment already has it.

From the repository root:

```powershell
python tools/ui-remote.py self-test
python tools/ui-remote.py --port COM12 shot captures/screen.png
python tools/ui-remote.py --port COM12 tap 848 564
python tools/ui-remote.py --port COM12 shot captures/after-tap.png
```

A tap is real UI input and can invoke machine controls. Enter `demo on` before
automated machine-control tests: preview disconnects UART1 and supplies synthetic
controller responses. Outside preview, isolate the machine before testing controls.
Coordinates are LCD pixels, with (0, 0) at the upper left; this display is 1024x600.
Single taps, held presses and single-pointer swipes are implemented. Physical
two-finger pinch remains available to the G-code viewer but is not injected.

Build normally and flash the application with:

```powershell
idf.py -p COM12 app-flash
```

This leaves the bootloader, partition table, NVS and file storage intact.
The board's CH340 supports automatic bootloader entry and RTS reset.

The firmware takes the display lock and copies the single RGB565 LCD buffer
through an internal-RAM row into a PSRAM snapshot, then releases the lock before
sending. Direct PSRAM-to-PSRAM copies triggered watchdog resets on this P4.
The current implementation requires one framebuffer and RGB565; other configurations
return an error. Physical touches and normal UI updates remain available after
the copy. A separate LVGL pointer device supplies injected press/release events, with
callbacks dispatched by the normal LVGL task.

Wire commands are ASCII lines: `UG SHOT` and `UG TAP x y`. Captures contain a
`@UGSHOT width height crc32` header, ordered `@UGROW y data` lines, and `@UGEND`.
Each row is run-length encoded as eight hex digits per run: four for the pixel
count followed by four for the RGB565 value. CRC32 covers the full little-endian
RGB565 image. The host rejects invalid runs, missing rows and checksum errors
before writing a PNG. Ordinary log lines are ignored. Capture times depend on
screen complexity; this is intended for screenshot-and-act workflows, not video.

The serial endpoint has the same physical access boundary as the console.
It is currently compiled into this firmware and reserves UART0 for the console.

For a sequence, keep one connection open so serial-open resets cannot interrupt
navigation. Start `python tools/ui-remote.py --port COM12 session`, then enter:

```text
demo on
touch off
rx <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0>
shot captures/jog.png
tap 848 564
shot captures/settings.png
tap 176 564
touch on
quit
```

The same lines can be piped to session from a PowerShell here-string or a text
file. Closing/reopening COM12 may reset CH340-connected boards, so use session
for multi-step workflows. Allow the normal LVGL task to render between taps
and captures; capture does not force a render from the console task.

Additional debug commands:

- `UG SWIPE x1 y1 x2 y2`: inject a roughly 0.4-second drag for scrolling/sliders; replies `@UGOK TAP`.
- `UG HOLD x y milliseconds` (50–2000): hold and release through LVGL; replies `@UGOK TAP`.
- `UG TOUCH OFF` / `UG TOUCH ON`: temporarily disable/restore physical touch for deterministic checks.
  Always restore ON in cleanup. Reset also restores touch.
- `UG TEST`: run embedded model tests with captured fake transmissions.
- `UG DEMO ON` / `UG DEMO OFF`: explicit preview mode; actual controller UART is disconnected.
- `UG RX line`: inject controller feedback in preview only.
- `UG STATE`, `UG LAST`, `UG PREFS`: inspect state, last non-poll transmission and settings.
- `UG SETTING FIELD <ASCII-hex>`: fill the open machine-setting or Find field;
  this does not save. Use demo mode for automated setting-write checks.
- `UG WIFI STATE`: inspect actual C6 scan/connection state, RSSI, SSID, IP and scan names.
- `UG WIFI PASSWORD hex`: fill the currently open password field from printable
  ASCII encoded as hex, without echoing it. This still requires selecting a scanned
  network and pressing Connect through the UI. It does not bypass validation.

The Python session supports `demo on`, `rx <Idle|...>`, `test`, `state`, `last`,
`prefs`, `touch off/on`, `hold x y ms` and `swipe x1 y1 x2 y2`. `tools/check-jog.py` exercises held touches directly.

`tools/check-workflow.py` checks LCD units, settings scrolling, the Probe modal, SD
row actions, job navigation lock, overrides, completion and Stop. It restores touch
in cleanup. Add `--no-captures` for assertions only.

`tools/check-tabs.py` checks both tab directions and interrupted transitions in
UART-disconnected demonstration mode, restoring physical touch afterward.

## Refresh the README gallery

From the repository root, using Python with pyserial:

```powershell
python tools/check-theme-gallery.py
python tools/check-fullscreen.py
```

Start in metric, incremental CNC mode with no job running. The theme gallery
captures Jog modes and disabled controls, both SD listings, cached views and the
14-line text fallback. The development SD card must contain `/mb_83.nc` with
current cached views and the uncached `/rotary/3D_Finish_1_1.gcode` example.
Run **Update previews** from
Files first when the cache is missing. The pan capture exposes the image frame
so a light canvas backing cannot go unnoticed.

The full-screen check covers Wi-Fi, Probe, Tools, confirmations, controller
settings, macro editing, actuator configuration and job/error pages. It uses
UART-disconnected feedback, cancels motion and settings changes, and restores
physical touch. Wi-Fi checks use the actual C6 radio and the approved development
network, Braai-Fi. Supply its password only when testing a successful connection:

```powershell
$galleryPassword = Read-Host 'Wi-Fi password' -AsSecureString
$env:PANEL_TEST_WIFI_PASSWORD = [System.Net.NetworkCredential]::new('', $galleryPassword).Password
try {
    python tools/check-fullscreen.py
} finally {
    Remove-Item Env:PANEL_TEST_WIFI_PASSWORD
}
```

Both tools support `--port`, `--captures PATH` and `--only NAME ...`. Their default
folders are ignored `captures/readme-refresh/` and `captures/fullscreen/`.
Check the exit code, open every PNG and confirm the intended state before copying
accepted images into `docs/screenshots/`. Inspect every image linked by README,
including alternate Jog states and renderer exports; refreshing the main screens
alone leaves stale images in the gallery. Publish only inspected images, not
serial logs or credentials. The earlier `capture-gallery.py` sequence uses the
previous layout; use the current tools above for this interface.

## Capture failures and input cleanup

Only one process can own COM12. Close serial monitors and other test sessions.
The utility opens with DTR/RTS deasserted and probes `UG SYNC` until the application
answers, then clears old input. `UG SYNC` is intentionally invalid and harmless.

Do not accept an incomplete image: missing rows, invalid RLE, CRC mismatch or a
device reset are failures. Wait for any ongoing transfer to finish and synchronize
again before retrying. Ordinary SDK log lines are ignored, but a log or panic that
interrupts an encoded row invalidates that capture. Never remove the CRC check to
make a broken capture pass. Preserve reset logs when diagnosing firmware crashes.

Automation should put `UG TOUCH ON` in `finally` cleanup. After a killed process,
open a fresh synchronized session and send `touch on`; a reset also restores touch.
An interrupted screenshot may need to finish before the console can accept this.
The gallery and check scripts demonstrate the complete pyserial lifecycle and call
`remote.debug()` for console commands and `remote.exchange()` for taps/screenshots.

## Firmware pieces to keep

The source map is in [AGENTS.md](../AGENTS.md#code-to-retain-when-porting-this-workflow).
`ui_remote.c` owns UART0, RLE encoding and the extra LVGL pointer device.
`bsp_display_copy_framebuffer()` supplies the RGB565 pixels. `panel_runtime.cpp`
implements UART-off preview and injected controller reports; `panel_ui.cpp` supplies
settings inspection and the masked Wi-Fi password hook. `tools/ui-remote.py`
decodes and validates frames before producing PNGs without image-library dependencies.

The startup order in `main/main.c` is:

```c
bsp_display_lock(0);
panel_ui_create();
bsp_display_unlock();
ui_remote_start();
```

When porting, compile `ui_remote.c`, include `ui_remote.h`, implement the board's
framebuffer-copy helper and start the console after display/UI initialization and
after releasing the display lock. The board-specific panel handle and input device
come from the BSP; keep that hardware code alongside the console source.

Keep touch callbacks and rendering on the LVGL task. Keep the internal-RAM row
staging when copying the framebuffer; a bulk PSRAM-to-PSRAM copy caused watchdog
resets on this board. Retain the tested SDK configuration, including disabled
PSRAM instruction/rodata fetching for the ESP-Hosted Wi-Fi path.

The staging passes use explicit volatile byte loops. Row-sized ROM `memcpy`
calls also caused intermittent capture-time memory corruption on this display;
repeated page/dialog captures passed after removing that path. Retain both loops.
`@UGDBG` phase markers distinguish heap checks from framebuffer copying, and
`UG STATE` includes `reset_reason`. On a reset, the host utility preserves the
ROM reset header and initial boot lines alongside the diagnostic trace.
Keep the staging row and RLE output buffer in internal RAM. The PSRAM snapshot
is cache-line aligned and written back before releasing the display lock. A
damaged packet is rejected and drained through END so touch cleanup can run;
the image still requires all rows and the matching CRC before it is saved.


## Display SD and cached views

`UG SD STATE` reports mount state, file count, current page, preview server and
cache readiness for the four visible files. Files starts on Display SD. Select
Controller SD explicitly before running controller-file demo tests.
`UG PREVIEW SERVER <http-url>` fills the open Settings editor; Save commits it.
`UG PREFS` includes `preview_view` (0-7) and `local_preview` for UI checks.

`tools/check-display-sd.py` validates the actual development card's catalogue and
isolated character-counting streaming. `tools/check-preview.py` checks cached
native bitmap views. See [Display SD](display-sd-and-previews.md) for server setup,
cache format, sender limits and hardware memory requirements.

Capture cooperatively pauses background preview transfers and hashing; it does not pause a running G-code sender. `storage_busy` rejects capture if I/O has not yielded within 20 seconds.

`tools/check-file-location.py` checks both Settings file locations across RTS
reboots and leaves Onboard SD selected. `tools/check-handoff-sd.py --no-captures`
checks gCAM uploads from Jog with the shared Settings source.

`tools/check-text-fallback.py` checks the real 14-line file-start fallback with
the PC preview service temporarily stopped. It adds a small uniquely named
header sample on onboard SD, captures the LCD and checks the top-left back arrow.
It uses UART-off demo mode, never runs the job, and restores physical touch.
Restart the preview service after this check.
## Capturing selected documentation states

Use `--only <screenshot-name> ...` with `check-theme-gallery.py`, `check-fullscreen.py`,
`check-machine-settings.py` or `check-tools.py` to retry interrupted images
without transferring completed captures again. Navigation and checks still run.
`capture-readme-extras.py --scene <name> ...` captures the remaining UI states, cached large-file views and actuator
configuration dialogs using isolated demo feedback. Its `--reset`
option restarts the display before a sequence. Inspect every image before copying
it into `docs/screenshots`; rejected or wrong-state images must stay out of Git.

The host drains malformed/non-ASCII/out-of-order screenshot rows through `UGEND`
before reporting failure, so queued touch-restoration commands are not mistaken
for image data. Rejected frames never replace documentation images. The decoder
self-test includes log-contaminated rows and subsequent valid rows.

`ui-remote.py` batches available serial bytes on Windows. The gallery buffers
frame-row logging and flushes at protocol boundaries rather than after every
row. Keep these paths buffered; per-byte reads and per-row disk flushes made
large captures exceed the host deadline. `check-macro-grid.py --short-samples`
uses compact G21/G90 fixtures for photos; its empty-library safeguard remains.

### Manual preview preparation

`UG SD STATE` exposes `update_active`, `update_done` and `update_failed` as well
as `preparing`, `working` and `percent`. Normal Files browsing leaves both active
flags false. Use Files > Update previews to start a batch; Cancel stops it.
Cached preview selection uses a small persistent path index and does not start
hashing or networking. `tools/check-preview-update.py` exercises this on the LCD
in UART-disconnected demo mode and restores physical touch afterward.

### Dialog checks

`UG PREFS` reports `dialog` and `number_value`. Assert dialog identity before
injecting keypad or popup actions. Dialogs use opaque full-screen backing without
translucent dimming or floating shadows, and block background machine actions.

When restoring source backups, update their modification times or run `idf.py clean`
before building. Preserved older timestamps can leave a newer experimental object
in the firmware even though the source has been restored. Keep the LCD draw buffer
in internal DMA RAM (1024 x 50 pixels); no full-frame PSRAM drawing buffer.

The full-screen Probe Configuration editor has a left settings card for X/Y/Z,
feed and corner inset, and a right card with the shared numeric keypad. Each
probe-setting entry point focuses its field; Cancel returns to the Probe page
when opened there.
`UG SETTING FIELD <ASCII-hex>` fills its currently focused field without applying;
it still fills machine-setting/search fields when that editor is open. `UG PREFS`
reports `dialog=probe-offsets` and the draft values as `probe_edit_x/y/z`. Saved
values remain in `x/y/z`, `feed` and `inset`; draft feed/inset use
`probe_edit_feed` and `probe_edit_inset`. Validate all five before changing any.

### Diagnostics page

The Diag pill opens the read-only Diagnostics page (page ID 6), immediately
before Settings. Its LEDs use the existing input parser; missing or stale input
reports stay unknown. No input-diagnostics row remains in Settings.

`python tools/check-diagnostics.py --port COM12` checks all five tabs, extended
probe retention/release, standard limits, disabled pin reporting, Probe Cancel
and unchanged preferences. It captures the main screens and input states into
`captures/diagnostics/` with UART1 disconnected, then restores physical touch.
Inspect those real framebuffer images before publishing them to the README.

### Full-screen secondary pages

Wi-Fi scan/password/result, Probe, Tools/spindle choice, numeric input,
confirmations, machine settings/search/editors, actuator configuration, macro
editing, preview server and preview-update progress use the main graphite/card
palette. Probe Configuration uses separate settings and keypad cards in that same theme.
The old `modal` variable hosts a fully opaque page; it is not a floating overlay.

`UG PREFS` includes `form_kind` and the actual content page `form_w`/`form_h`.
Assert these before interacting or capturing; a tap alone does not prove that the
expected page opened. `tools/check-fullscreen.py` checks return paths and Cancel,
keeps controller UART off, and restores physical touch and application mode.
`--only NAME...` limits captures while retaining functional checks. Optional
`PANEL_TEST_WIFI_PASSWORD` is read only from the environment and never logged;
connection checks target Braai-Fi only. No macro or machine setting is saved.

The Wi-Fi worker defers a scan queued during saved-network startup until the
connection attempt finishes; an early scan must not cancel the startup reconnect.

## Live serial diagnostics

`tools/check-live-serial.py --isolated-controller --stage inspect` records a live
controller baseline. Use `--output` for a separate run directory. The other
stages exercise actual controller replies; they are not demo tests. Read the
[test plan](live-controller-test-plan.md) and [recorded results](live-controller-results-2026-10-27.md)
before selecting motion, homing or fault stages. Confirm machine isolation first.

The UART session accepts `audit` and `audit INDEX` (`UG AUDIT` on the wire):

- No index: lifetime command/poll/error/reset counters, current stream counts,
  FNV-1a wire hash, outstanding-byte peak, memory and actual machine state.
- Nonnegative index: one controller setting ID, group and value; an out-of-range
  index returns `end: true`. These are read-only.
- -2 through -15: cached VER, OPT, GC, G54–G59, G28, G30, G92, TLO and PRB reports.
- -16: latest raw realtime status; -17: latest actual `[MSG:...]` report.
- -18: current controller fault description and cached alarm/error counts.

Stream counters reset on a new file and survive cancellation for diagnosis.
Lifetime counters reset when the pendant reboots; intentionally induced faults
therefore remain visible through later tests. FNV verifies the transmitted
normalised ASCII plus LF, not the original file's comments or spacing.

`sd REMOVE TEST NAME` removes only filenames beginning `000-00-serial-` from
the currently selected SD source with no active work (controller SD also needs
an idle connection). It rejects paths
and traversal. This console-only command is for removing test fixtures, not a
general file manager. On controller SD, wait for acknowledgement and refresh
the listing to verify deletion. Never run an existing user file as a fixture;
verify the selected filename in the Start confirmation before approving it.

Reboot the pendant after embedded `test` checks before measuring production
memory: the embedded test models retain their allocated vectors for reuse.

Fault descriptions are read with the structured `$EA` and `$EE` reports after
identification, before jobs begin. `$EAG`/`$EEG` are the CSV variants; see
[grblHAL report extensions](https://github.com/grblHAL/core/wiki/Report-extensions#alarm-codes).
The controller defers ordinary command processing in Hold, so fetching a catalogue
only after a streamed error is too late. Keep these reads in the acknowledged
startup queue. Unsupported enumeration preserves the original fault and fallback.

The runtime supplies an optional 256-entry external-RAM buffer, keeping catalogue
strings out of the internal UI heap. Retain the explicit byte copies used for
this P4 revision. Entries reset on reconnect, support driver/plugin code numbers,
and truncate displayed text to 191 bytes. Overflow, allocation failure and unknown
codes retain fallback descriptions. Local sender errors never borrow a controller
description merely because they reuse a numeric code.

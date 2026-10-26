# Device UI development

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
python tools/capture-gallery.py --section core
```

Cached views and the Preview server dialog are captured by `tools/check-preview.py`.

This captures Jog modes and disabled controls, metric/inch settings, probing and
numeric input, Home/Zero confirmations, controller file listing,
Run confirmation, running/paused/completed jobs, alarms and command errors.
It restores the original display-unit preference and physical touch, and leaves
preview on the simulated Files list. Home/Zero confirmations are cancelled; no
real controller is connected by the script.
Start on Jog, Files or Settings with no dialog open. A failed run leaves its
current screen available for inspection; dismiss that dialog before rerunning.

Wi-Fi captures use the actual C6 radio. They scan, show the empty password field
and its validation, deliberately try an invalid password, then reconnect to the
supplied network and capture success. Choose a target among the first four scan
results. Supply its password only through a temporary environment variable:

```powershell
$galleryPassword = Read-Host 'Wi-Fi password' -AsSecureString
$env:PANEL_WIFI_PASSWORD = [System.Net.NetworkCredential]::new('', $galleryPassword).Password
try {
    python tools/capture-gallery.py --section wifi --ssid 'Your SSID'
} finally {
    Remove-Item Env:PANEL_WIFI_PASSWORD
}
```

Use `--section all` for both sequences, or `--output PATH` to choose the capture
folder. The default is ignored `captures/gallery/`. Check the process exit code,
open every PNG and check its intended screen before copying accepted images into
`docs/screenshots/`. The script deliberately does not publish unchecked images.
Password-entry images use an empty field; credentials are not saved in the script
or printed by the password injection hook.
Raw serial output is retained in the ignored capture folder's `serial.log` for
diagnosing rejected frames. Publish only the inspected PNGs, not these logs.

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

Use `--only <screenshot-name> ...` with `capture-gallery.py`,
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

The full-screen Probe Configuration editor has one shared numeric keyboard for
X/Y/Z, feed and corner inset.
`UG SETTING FIELD <ASCII-hex>` fills its currently focused field without applying;
it still fills machine-setting/search fields when that editor is open. `UG PREFS`
reports `dialog=probe-offsets` and the draft values as `probe_edit_x/y/z`. Saved
values remain in `x/y/z`, `feed` and `inset`; draft feed/inset use
`probe_edit_feed` and `probe_edit_inset`. Validate all five before changing any.

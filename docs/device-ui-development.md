# Device UI development

The firmware in firmware/viewesmart-p4 accepts two commands on its existing UART0 console
(COM12, 115200 baud). This captures the physical LCD framebuffer, including custom
drawing, rather than a simulated UI.

Close other serial monitors before using the utility. Install Python's pyserial
package if needed; the local ESP-IDF Python environment already has it.

From the repository root:

```powershell
python tools/ui-remote.py self-test
python tools/ui-remote.py --port COM12 shot captures/screen.png
python tools/ui-remote.py --port COM12 tap 512 300
python tools/ui-remote.py --port COM12 shot captures/after-tap.png
```

A tap is real UI input and can invoke machine controls. Disconnect the CNC
controller before automated UI exploration. Coordinates are LCD pixels, with
(0, 0) at the top left. Only single taps are implemented.

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
shot captures/home.png
tap 650 420
shot captures/setup.png
tap 70 565
shot captures/home-again.png
quit
```

The same lines can be piped to session from a PowerShell here-string or a text
file. Closing/reopening COM12 may reset CH340-connected boards, so use session
for multi-step workflows. Allow the normal LVGL task to render between taps
and captures; capture does not force a render from the console task.

Additional debug commands:

- `UG SWIPE x1 y1 x2 y2`: inject a 400 ms drag for scrolling/sliders; replies `@UGOK TAP`.
- `UG HOLD x y milliseconds` (50–2000): hold and release through LVGL; replies `@UGOK TAP`.
- `UG TOUCH OFF` / `UG TOUCH ON`: temporarily disable/restore physical touch for deterministic checks.
  Always restore ON in cleanup. Reset also restores touch.
- `UG TEST`: run embedded model tests with captured fake transmissions.
- `UG DEMO ON` / `UG DEMO OFF`: explicit preview mode; actual controller UART is disconnected.
- `UG RX line`: inject controller feedback in preview only.
- `UG STATE`, `UG LAST`, `UG PREFS`: inspect state, last non-poll transmission and settings.

The Python session supports `demo on`, `rx <Idle|...>`, `test`, `state`, `last`,
`prefs`, `touch off/on`, `hold x y ms` and `swipe x1 y1 x2 y2`. `tools/check-jog.py` exercises held touches directly.

`tools/check-workflow.py` checks LCD units, settings scrolling, the Probe modal, SD
row actions, job navigation lock, overrides, completion and Stop. It restores touch
in cleanup. Add `--no-captures` for assertions only.

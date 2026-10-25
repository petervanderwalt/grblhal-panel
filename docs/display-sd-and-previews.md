# Display SD and cached previews

Files has two sources:

| Source | Browsing | Execution |
| --- | --- | --- |
| Display SD | Four rows per page, with Preview and Run | The pendant streams the file over the controller UART. |
| Controller SD | File list and Run, without preview downloads | grblHAL runs its own SD file. |

The display card uses FAT and long filenames. Existing files are never formatted
or rewritten. Copy G-code onto the card with a reader, then insert it before
booting. With Onboard SD selected in Settings > File location, gCAM handoff can also save new
files up to 2 MiB over Wi-Fi; existing filenames are rejected. Refresh rescans the card. The current catalogue supports 512 G-code
files, paths shorter than 160 ASCII characters, and folders up to four levels
deep. Restart the display after changing or removing the card.

## Preview server

On the PC, install Python dependencies and start the service on its LAN address:

```powershell
python -m pip install -r preview-server/requirements.txt
python preview-server/server.py --bind 192.168.0.100 --port 47821 --allow-client 192.168.0.34
```

Replace the address with the PC's address on the same network as the pendant.
The default bind address is localhost. Specify the PC LAN address and permit
the display IP with `--allow-client`. Other remote clients receive HTTP 403. Permit only the intended display through the PC firewall.
The service is for a trusted LAN, with no public exposure or internet dependency.

In **Settings > Preview server**, enter `http://192.168.0.100:47821` and Save.
An empty address disables new server requests; already cached images still work.
The address persists across restarts. The PC must be running while generating
new previews. It is not required for browsing cached images or running jobs.

While the machine is idle, the pendant hashes each file and checks its cache.
Missing previews are uploaded to the PC as a streaming HTTP body. The server
checks the SHA-256, generates eight views, and the pendant downloads native RGB565 bitmaps
into `.panel-previews/v2-<sha256>/` on the display card. A completion marker is
written only after every bitmap arrives. Original G-code is read-only throughout.
A changed file gets a different cache key. Incomplete caches are retried.

Available views: Top, Bottom, Front, Back, Left, Right, Isometric and Perspective.
Images are 740 x 328 pixels. The server retains PNGs and supplies native LVGL RGB565 files to the pendant, avoiding runtime PNG decoding. Each cached view is 485,452 bytes; eight use about 3.9 MB. Drag an image to pan; use a physical two-finger pinch
to zoom. This is a toolpath image, not collision checking or stock simulation.

The server accepts files up to 512 MiB and geometry up to 15 million segments.
It handles linear moves, IJK and R arcs in all three planes, helical arcs,
metric/imperial units and absolute/relative positioning. Unsupported expressions,
coordinate transforms, tool-change-dependent geometry and additional axes fail
explicitly rather than presenting a partial preview. Preview failure does not
prevent a separate confirmed Run of G-code supported by the controller.

The server API is deliberately small:

- `GET /health`
- `POST /v2/jobs/<sha256>` with the raw G-code and Content-Length; returns 202.
- `GET /v2/jobs/<sha256>/manifest.json`; 202 while working, then ready/error JSON.
- `GET /v2/jobs/<sha256>/<view>.bin` (or `.png`) for a finished view.

The service writes only its own `preview-server/cache` directory. It processes
one generation job at a time. Change the renderer version and firmware cache
version together when changing the image format or interpretation.

## Streaming

Display-SD Run requires an identified controller with fresh Idle feedback and
user confirmation. The complete file is checked for line length and unsafe
serial characters before any G-code is transmitted. Checking is incremental so
Stop and the display remain responsive; the file is never loaded wholly in RAM.

The sender follows Ooznest Control's character-counting window. It counts the
actual transmitted ASCII bytes **including LF**, keeps fewer than 128 bytes
outstanding, and frees each oldest command on its acknowledgement. Realtime
status, hold, resume and overrides bypass that window. Comments and whitespace
are stripped; an executable block can contain at most 126 characters before LF.

Hold stops new commands immediately. An error, alarm, SD read failure, lost
connection or unexpected controller reset cancels streaming. Errors request feed
hold; recovery offers confirmed Stop/reset. Nothing resumes automatically after
reboot. Buffered controller motion is not cancelled merely by closing a file,
which is why Stop resets the controller.

Progress uses acknowledged file offsets. EOF is insufficient for completion:
all commands must be acknowledged and a later controller report must be Idle.
The pendant then refreshes modal state before returning to manual controls.
The existing Job page supplies pause/resume, confirmed Stop and overrides.
Manual tool-change handling is not implemented.

At 115200 baud there is still a limit on G-code sent during execution. Display
storage removes the pre-run download and PNG rendering cost, but does not remove
the UART throughput limit for dense jobs. Actual-controller testing is required.

## Development checks

```powershell
python preview-server/test_renderer.py
python tools/check-display-sd.py --port COM12
```

The device check requires the development card's `arc-ijk.gcode` and
`MB-14.5 test.nc` examples. It disables the controller UART with Demo before any
injected touches, verifies every catalogue page, streams actual card files
against simulated responses, and restores physical touch. `UG SD STATE` reports
the card, current page and cache state. `UG PREVIEW SERVER <url>` fills the open
server-settings field only; Save is still a real UI action.

Portable and embedded stream tests cover the 128-byte window, acknowledgements,
validation before motion, hold, timeout, faults, resets and fresh-Idle completion.
Physical controller execution and physical two-finger pinch need hardware checks.

## Hardware integration

The card and C6 share the P4 SDMMC host on separate slots. Mount failure must
release only the card slot, and must never format the card. The tracked CMake
patch keeps Hosted's memory pool in PSRAM while staging SDIO data through two
64-byte-aligned internal 512-byte buffers. IDF 5.3 rejects unaligned block DMA
buffers. An internal-only pool leaves too little memory for startup tasks.
Idle and interrupt stacks are 4096 bytes. Cached images load through a 1 KiB
internal staging buffer into one 485,440-byte RAM canvas. View changes reuse that
buffer; pan/zoom reads it without accessing SD. Capture pauses background
preparation and flushes its snapshot in bounded cache-line-aligned blocks.
`panel_memory.c` applies the same internal staging to library copies involving
external RAM on this P4.

Native bitmap caches use a versioned directory. Old cache versions are ignored;
original G-code remains untouched. Removing `.panel-previews` with a card reader
removes only generated images and causes regeneration when the service is available.

## Preparation progress

Files shows an animated indicator while the idle worker prepares previews.
Opening Preview shows the current stage, elapsed time, and a progress bar when
measurable: SD reading/checksum, Wi-Fi upload, server toolpath reading, eight-view
rendering, and eight-view download. Connecting and waiting stages keep the
spinner and elapsed timer visible without inventing a percentage. Errors stop
the activity indicator and show the failure message. Cached previews skip the
upload and rendering stages.

Pending server manifests include `phase` (`parsing` or `rendering`), `percent`,
`elapsed` in seconds, and `view`/`views` during rendering. Parsing percentage
tracks source bytes; rendering percentage tracks the current view. The pendant
combines view progress into the eight-view progress bar.
`tools/check-preview-progress.py` checks changing percentages on an actual large
SD file and captures the LCD with controller UART disconnected.

## Text fallback

When no cached image is available, Preview shows the first 14 lines read directly
from the onboard G-code file. Comments, headers and blank lines are retained.
This also works with no preview server or Wi-Fi connection. The same text stays
visible while preview generation runs; a completed image replaces it automatically.
Progress appears beside the text, tied to the selected file. A missing or corrupt
image leaves the file-start text visible. Run remains a separate confirmed action.

The reader consumes at most 4 KiB and displays at most 80 characters per line,
marking longer lines with an ellipsis. This is a first-screen review, not an
editor or a substitute for checking the whole program. It does not send G-code.
Controller SD still offers listing and Run only; its file content is not downloaded.

Preview navigation uses a top-left back arrow. Confirmation dialogs keep their
Cancel/Run or Cancel/OK actions.

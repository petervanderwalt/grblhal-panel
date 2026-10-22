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
shot captures/home.png
tap 650 420
shot captures/setup.png
tap 70 565
shot captures/home-again.png
quit
```

Keep one session open for multi-step navigation. Session commands can also be
piped from a text file. Only one serial client may own COM12. The host probes the
console before sending commands because boot/open timing can vary. It fails a
capture if the device resets, rows are missing, RLE is invalid, or CRC32 differs.

Inspect every resulting PNG before claiming a UI task complete. Test at least a
navigation round trip. A capture takes seconds and depends on image complexity;
do not treat this as video.

Taps invoke real LVGL events and may activate machine controls. The user confirmed
the CNC was disconnected for the initial tests. Reconfirm isolation before future
tests involving motion, homing, probing, spindle, or job execution. Do not assume
that today's disconnection remains true indefinitely.

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

Design for landscape, two-handed pendant use. Keep large XY targets at the left,
Z at the right, DROs above the hands, and bottom navigation. STOP must stay visible
on all pages and above dialogs, and send Ctrl-X on touch-down without confirmation. Keep confirmations
for homing, zeroing, probing and starting jobs. Per-axis zero is on each DRO;
individual homing is on Zero / Home. Update injected-touch coordinates when moving controls.

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

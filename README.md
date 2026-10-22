# grblHAL panel

Firmware development for the VIEWESMART 7-inch 1024x600 ESP32-P4/C6 touch display.

The initial firmware is the user's uGsender UI baseline, with real hardware
screenshot capture and injected LVGL taps over the COM12 USB UART. The grblHAL
panel UI and controller transport work will build on this baseline.

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

The device was verified through Home → Machine Setup → Home using real captures
with CRC checks. Single taps are supported; swipes and multitouch are not.

Board source and documentation:
[VIEWESMART hardware](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
Existing component licenses are retained in their source directories.

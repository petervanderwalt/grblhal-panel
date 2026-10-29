# Display-mirror-usb

USB monitor and touchscreen test firmware for the VIEWESMART 7-inch 1024x600 ESP32-P4/C6 display, tested on ESP32-P4 revision 1.3 with Windows.

The display receives JPEG frames over USB and reports up to five touch contacts. The current transport limit is 15 FPS; approximately 14-15 FPS was observed during animated desktop updates. Audio is disabled.

This is a separate copy of `firmware/viewesmart-p4`. Its LCD setup and memory workarounds are retained. The copied pendant sources in `main/` are reference material; only `main/panel_memory.c` is included in the USB monitor build. Controller transports are not started.

## 1. Connect the cables

| Board socket | Connect to | Purpose |
| --- | --- | --- |
| **UART** | PC | Flashing firmware and reading logs; COM12 on the development PC. |
| **USB2** | PC, using a second data cable | USB display and touch, high speed at 480 Mbps. |
| **USB3** | Leave unused for this test | Full-speed 12 Mbps socket previously used for the controller. |

Keep UART connected during development. Use USB2 for the monitor connection and retain the board's normal power arrangement.

## 2. Build and flash

Use **ESP-IDF 5.3.4**, as used by the original pendant firmware. Open an ESP-IDF PowerShell terminal, enter this folder and run:

```powershell
$env:IDF_TARGET='esp32p4'
idf.py build
idf.py -p COM12 app-flash
```

Replace `COM12` with the board's UART port shown in Device Manager. The first build downloads the pinned managed dependencies. `sdkconfig.defaults` contains the board settings; `dependencies.lock` records the resolved versions.

On the development PC, the existing IDF environment can be loaded with:

```powershell
. 'C:\Users\User\Documents\7 Inch Pendant\esp-idf-full\export.ps1'
```

`app-flash` updates the application on a board already running this project's bootloader and partition layout, preserving NVS and file storage. For a blank board, use `idf.py -p COM12 flash` to write the bootloader, partition table and application. Do not run `erase-flash` as part of normal setup.

To read startup logs:

```powershell
idf.py -p COM12 monitor
```

Exit the monitor with **Ctrl+]** before flashing again. Only one program can use the UART port at a time.

## 3. Install the Windows driver

Supported host: **Windows 10 or Windows 11**.

1. Open [windows-driver/xfz1986_usb_graphic_250224_rc_sign.exe](windows-driver/xfz1986_usb_graphic_250224_rc_sign.exe).
2. Approve the Windows administrator prompt if shown, then select **Install** in the installer.
3. Connect or reconnect the board's **USB2** cable.
4. In Device Manager, confirm that **xfz1986_usb_graphic(demo)** appears under Display adapters and **HID-compliant touch screen** appears under Human Interface Devices.

This composite device uses **VID 303A / PID 2986**: interface 00 is the display and interface 01 is touch. Windows supplies the HID touch driver; the bundled installer supplies the USB display driver.

The bundled installer is an unmodified copy of the signed installer linked by [Espressif's Windows driver instructions](https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/device/usb_extend_screen/windows_driver). [Original download](https://dl.espressif.com/AE/esp-iot-solution/xfz1986_usb_graphic_250224_rc_sign.exe). Its Authenticode signature was verified as valid when downloaded. The SHA-256 checksum is recorded in [windows-driver/SHA256SUMS](windows-driver/SHA256SUMS).

To check the downloaded file locally:

```powershell
Get-FileHash .\windows-driver\xfz1986_usb_graphic_250224_rc_sign.exe -Algorithm SHA256
Get-AuthenticodeSignature .\windows-driver\xfz1986_usb_graphic_250224_rc_sign.exe
```

## 4. Set the display resolution

1. Open **Windows Settings > System > Display**.
2. Select **Identify**, then select the ESP32 panel in the monitor layout.
3. Select **Extend desktop to this display** and set its resolution to **1024 x 600**. Start with **100%** scaling.
4. Drag its monitor rectangle to match its physical position, then apply the changes.

Windows may retain a **320 x 240** desktop from the driver's first detection; explicitly select 1024 x 600 if the image looks enlarged or blurry. Windows may list a 60 Hz display mode; USB image updates are currently limited to 15 FPS.

## 5. Assign touch to the correct monitor

With multiple monitors, Windows may initially send touches to a normal monitor. Assign the touchscreen once:

1. Open **Control Panel > Tablet PC Settings**. Search Control Panel for `Tablet PC` if needed.
2. On the **Display** tab, select **Setup** under Configure.
3. Choose **Touch input** if prompted.
4. When the identification prompt appears on a normal monitor, press **Enter** to move it to the next display.
5. When the prompt appears on the **ESP32 panel**, physically touch that panel.
6. Press **Enter** to finish if prompted.
7. Test taps near opposite corners and drag a window or scroll a page on the panel.

The firmware disables the inherited X/Y mirroring to correct the 180-degree touch rotation on this board. Touch coordinates should match the displayed desktop after Windows associates the touch device with this panel.

Repeat Setup if Windows loses the assignment after a driver reinstall or monitor-layout change.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No extra monitor | Use the USB2 data connection, install the bundled driver, then reconnect USB2. UART alone provides programming and power. |
| Enlarged or low-resolution desktop | Select the ESP32 display in Windows Settings and choose 1024 x 600. |
| Touch clicks another monitor | Repeat Tablet PC Settings > Setup and touch only the ESP32 panel when prompted. |
| Touch is rotated 180 degrees | Build/flash the current `usb_screen/app_touch.c`, which disables both inherited mirror flags. |
| Blank screen | Check that Windows has enabled the extra display and inspect UART for `USB monitor ready` and `USB Mount`. The LCD stays blank until frames arrive. |
| UART port is busy | Close other serial monitors before flashing or opening `idf.py monitor`. |
| Unexpected restart | Capture the complete UART log, including the reset reason and `Previous run` render breadcrumb, to distinguish a fault from a flashing reset. |

## Restore the pendant firmware

From an ESP-IDF 5.3.4 terminal, enter the original `firmware/viewesmart-p4` folder and run:

```powershell
idf.py build
idf.py -p COM12 app-flash
```

## Implementation and validation

JPEG decoding uses separate padded storage: a 600-row image may decode to 608 rows. Only the visible 600 rows are copied to the LCD. The LCD copy uses DMA2D and waits for completion before the next JPEG decode. This replaced the CPU framebuffer copy that reproduced watchdog resets on revision 1.3.

USB reception, image processing and touch tasks use CPU0. The USB descriptor advertises a 292 KiB JPEG limit, within the 300000-byte receive buffer. UART logs include frame counts, touch-down coordinates and the last render phase retained across watchdog resets.

The firmware was built with ESP-IDF 5.3.4 and app-flashed through CH340 UART COM12; esptool verified the written hash. The memory-copy self-check passed. Windows enumerated the display and HID touch interfaces. The DMA build completed a one-minute animated desktop test at approximately 14-15 FPS without a reset, panic or DMA timeout, and the user confirmed stable display operation. Touch input was detected; the final orientation correction is included along with the Windows monitor-assignment steps above. Higher frame rates, audio and extended-duration playback remain untested.

USB protocol, descriptors and frame handling are adapted from Espressif's [usb_extend_screen example at revision b3d71f9](https://github.com/espressif/esp-iot-solution/tree/b3d71f9ebb2bfbd3d80882162e546652ab0bbf4d/examples/usb/device/usb_extend_screen), which supports the installed IDF 5.3 toolchain. Original license notices are retained; see [ESPRESSIF-LICENSE](ESPRESSIF-LICENSE).

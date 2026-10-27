# Controller connections

## Hardware

The display is the [VIEWESMART 7-inch ESP32-P4/C6 board](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
The UART-labelled connector is the programming/debugging port (COM12 on the development PC). Reserve UART0 for development
screenshots and console input so controller traffic cannot be interpreted as UI
commands.

The manufacturer's local `examples/esp-idf/11_RS485_Test/main/uart_echo_example_main.c`
uses UART1, TX27, RX26, 115200 baud, no RTS/CTS, and LDO channel 4 at 3300mV.
Those pins belong to the vendor RS485 example, not the selected TTL UART transport. This is evidence of the vendor's
example configuration, not proof that the bus wiring or direction handling has
been verified on this specific board revision. Review the
[schematic](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY/blob/main/schematic/ESP32-P4-SmartDisplay.sch.pdf)
before wiring and testing.

## Full grblHAL serial on IO37 / IO38

The TTL UART transport uses fixed TX37 / RX38 to the controller's UART header. Only baud rate is editable; legacy saved pin values are ignored without changing the preferences layout.
This should use the full grblHAL sender protocol: commands, responses, status,
settings, alarms, realtime controls and flow control as required by the panel.

Verify 3.3V logic and ground first. Cross display TX37 to controller RX and display
RX38 to controller TX. Confirm those pins are free on the fitted board revision.
RS485 A/B is electrically different from these TTL UART pins.

Existing connection/configuration code is in
`firmware/viewesmart-p4/main/cnc_link.cpp` and `cnc_link.h`.
The inherited RS485 example uses TX27/RX26; the current TTL sender uses TX37/RX38.
Do not automatically rewrite saved controller settings during identification.

## Plugin_panel over RS485

[dresco/Plugin_panel](https://github.com/dresco/Plugin_panel) is a controller-side
panel plugin exposing Modbus RTU or CAN functionality. Its README lists the
Modbus build definitions `PANEL_ENABLE=1`, `MODBUS_ENABLE=1`, and
`MODBUS_MAX_ADU_SIZE=64`; CAN uses `PANEL_ENABLE=2` plus `CANBUS_ENABLE=1`.

This is a different application protocol from a normal full grblHAL sender stream.
The controller plugin initiates transactions: panel.c calls modbus_send() to
read panel input registers and write panel holding registers. The display would
therefore implement a Modbus RTU server (slave), not poll the controller as a
Modbus client. Defaults in panel.h are address 0x0A and a 50ms update interval.
Input registers 100-111 contain encoder/key data; holding registers starting at
100 contain controller state, overrides and axis positions. Review the complete
current register map and float byte order before implementing the server.

Source references: [read/write requests](https://github.com/dresco/Plugin_panel/blob/master/panel.c),
[defaults](https://github.com/dresco/Plugin_panel/blob/master/panel.h), and
[register map](https://github.com/dresco/Plugin_panel/blob/master/docs/modbus_registers.md).

Plugin_panel support is not implemented. Review the schematic and register map
before wiring an RS485 implementation; TTL UART/RS485 hardware checks remain pending.

## USB3 controller link

Settings can select USB3 CDC ACM instead of UART. USB3 uses the full-speed OTG host routed to GPIO24/25; keep the display UART connector connected to the PC for development. RP2350 2E8A:000A identification, status and grouped settings were read on the bare controller. Jog and job-streaming protocol checks passed on the bare controller; physical machine operation has not been tested. See [controller testing](controller-testing.md). Software disconnect/reconnect and USB isolation in demo mode passed. `tools/check-usb.py` repeats these checks on the isolated RP2350 development board; confirm machine isolation before running it.

A normal startup alarm is released by explicit Unlock (`$X`), following Ooznest Control `performUnlock()` in `js/ui/alarms_and_errors.js`. Unlock does not home the machine or establish its position. The panel automatically opens a fault confirmation when an alarm first appears; a generic `<Alarm>` report is shown as Machine locked rather than a fabricated alarm number.

USB3 was built against this workstation's ESP-IDF 5.3.4 tree with P4 full-speed host support. Stock older SDK support and other CDC ACM controllers have not been verified. The UART socket remains separate from the native USB host.

Logical Disconnect disables application TX/RX and lowers DTR while retaining the claimed CDC interface. Reconnect starts a fresh model identification without recreating USB endpoints. Physical loss or a transport error closes the handle. This avoids the intermittent reopen failure observed during repeated logical sessions.

The selected connection starts on pendant boot. Identification retries after a timeout while the link remains enabled. Explicit Disconnect disables retries for that session; the next boot connects again. USB open failures lasting five seconds with a device physically present trigger an SDK root-port power cycle to restart enumeration. Neither reconnect nor boot unlocks an alarm or resumes queued work.

The bare controller reports Alarm 10 for its power monitor as well as E-stop. Preserve that fault and require user confirmation; do not infer which physical cause occurred from the number alone.

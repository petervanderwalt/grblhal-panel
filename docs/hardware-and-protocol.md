# Controller communication investigation

## Hardware

The user's display is the [VIEWESMART 7-inch ESP32-P4/C6 board](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
COM12 is its CH340 UART programming/debugging port. Reserve UART0 for development
screenshots and console input so controller traffic cannot be interpreted as UI
commands.

The manufacturer's local `examples/esp-idf/11_RS485_Test/main/uart_echo_example_main.c`
uses UART1, TX27, RX26, 115200 baud, no RTS/CTS, and LDO channel 4 at 3300mV.
That matches the inherited CNC-link defaults. This is evidence of the vendor's
example configuration, not proof that the bus wiring or direction handling has
been verified on this specific board revision. Review the
[schematic](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY/blob/main/schematic/ESP32-P4-SmartDisplay.sch.pdf)
before wiring and testing.

## Full grblHAL serial on IO37 / IO38

The user's proposed alternative is TX37 / RX38 to the controller's UART header.
This should use the full grblHAL sender protocol: commands, responses, status,
settings, alarms, realtime controls and flow control as required by the panel.

Verify 3.3V logic and ground first. Cross display TX37 to controller RX and display
RX38 to controller TX. Confirm those pins are free on the fitted board revision.
RS485 A/B is electrically different from these TTL UART pins.

Existing connection/configuration code is in
`firmware/viewesmart-p4/main/cnc_link.cpp` and `cnc_link.h`.
Defaults remain TX27/RX26 for the inherited RS485 path until a transport is chosen.
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

Next practical steps: review the register map and schematic, choose UART sender
or RS485 panel operation, establish a read-only connection, then test status
display before enabling any machine action. Neither controller path has been
hardware-tested in this initial screenshot work.

## USB3 controller link

Settings can select USB3 CDC ACM instead of UART. USB3 uses the full-speed OTG host routed to GPIO24/25; keep the display UART connector connected to the PC for development. RP2350 2E8A:000A identification, status and grouped settings were read on the bare controller. Motion has not been tested. Software disconnect/reconnect and USB isolation in demo mode passed. `tools/check-usb.py` repeats these checks on the isolated RP2350 development board; confirm machine isolation before running it.

A normal startup alarm is released by explicit Unlock (`$X`), following Ooznest Control `performUnlock()` in `js/ui/alarms_and_errors.js`. Unlock does not home the machine or establish its position. The panel automatically opens a fault confirmation when an alarm first appears; a generic `<Alarm>` report is shown as Machine locked rather than a fabricated alarm number.

USB3 was built against this workstation's ESP-IDF 5.3.4 tree with P4 full-speed host support. Stock older SDK support and other CDC ACM controllers have not been verified. The CH340 UART socket remains separate from the native USB host.

Logical Disconnect disables application TX/RX and lowers DTR while retaining the claimed CDC interface. Reconnect starts a fresh model identification without recreating USB endpoints. Physical loss or a transport error closes the handle. This avoids the intermittent reopen failure observed during repeated logical sessions.

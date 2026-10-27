# grblHAL panel

[**Install firmware in your browser**](https://petervanderwalt.github.io/grblhal-panel/) · [Download tester releases](https://github.com/petervanderwalt/grblhal-panel/releases)

Use desktop Chrome or Edge with the display's **UART** USB socket. The installer
writes the bootloader, partition table and application while preserving Panel
settings and SD files. Builds target the VIEWESMART 7-inch 1024×600 P4/C6 hardware;
the C6 must already have compatible ESP-Hosted firmware. See
[browser installation and releases](docs/releases.md).


A touchscreen pendant for grblHAL controllers, built for the VIEWESMART
7-inch ESP32-P4/C6 display. CNC mode handles machine setup, controller SD files
and job progress. Display-SD jobs include cached image previews and buffered
USB or UART streaming; controller-SD execution remains available. Actuator mode provides individual X/Y/Z/A controls, with linear
or rotary A.

The CNC landscape layout puts the position readout on the left and the jog controls
on the right for two-handed use. Large controls, axis colours and clear alarm
screens keep the common machine operations within reach. Bottom tabs switch
between Jog, Files, Macros, Diag and Settings with a short sliding transition.

Settings also offers an **Actuator** mode for independent X/Y/Z/A
mechanisms. It replaces Jog, Files and Macros with an **Actuators** screen. Configure
each actuator's name, button labels, machine-coordinate endpoints, speed and
Buttons / Position / Presets / Repeat layout in Settings. Four vertical columns
provide large position sliders or a rotary A dial. Repeat travels back and forth
between endpoints, with Go, Stop and a vertical speed slider. Slider changes
apply on finger release. Homing and fresh controller feedback are required.
Saving A's Linear/Rotary selection also configures the connected controller and
checks its reported type before allowing A movement.

The initial actuator version moves one axis at a time. A read-only Wi-Fi status
page is available at `http://<display-ip>:47820/actuators`. Coordinated sequences,
browser motion commands and Home Assistant control are planned. See the
[Actuator mode guide](docs/actuator-mode.md) for setup, limitations and use cases.

## Machine setup

- **Jog:** eight-direction XY pad, separate Z controls, incremental moves or
  continuous movement while held. Releasing a continuous jog stops it.
- **Jog speed:** Fast / Med / Slow use 100% / 40% / 10% of the controller-reported
  maximum rate for the moving axes. Diagonals use the lower axis rate; jogging
  waits for valid rate reports. Metric steps are 0.1 / 1 / 10 / 100 mm, or
  imperial steps 0.01 / 0.1 / 1 / 10 inch.
- **Position:** compact machine and work coordinates, individual axis homing
  and zeroing, Home All, Set Zero All, Go to XY Zero and Go to Z Zero.
- **Work offsets:** G54â€“G59 selection beside the Probe control.
- **Probing:** single-surface Z, X-/X+ and Y-/Y+, plus XYZ corner probing.
  Z is selected initially, with editable plate thickness and feed. Lift the plate,
  touch it to the tool, then release to enable Probe. X/Y and corner modes use
  the tool diameter; dimensions and feed are configurable.
- **Spindle / coolant:** a Jog-screen button opens RPM, CW/CCW, spindle Off,
  mist and flood controls, plus discovery and activation of enabled spindles. Starting or switching the spindle requires confirmation. Controls
  use the controller's reported state and RPM limits and are available while idle.
- **Diag:** a dedicated tab shows live limits, probes, door,
  E-stop and other standard controller inputs. LEDs show red for active, green
  for inactive and grey for unknown or stale feedback.

## Macros

The Macros tab stores up to twenty named G-code sequences in a 4 Ã— 5 grid.
Use **Add macro** to enter a name and multiline G-code with the onscreen
keyboard, and choose a tile colour. Tap a tile to Run or its pencil to Edit.
Delete is inside the editor. Add stays in the header, leaving all twenty slots available.
Macros persist across restarts independently of controller SD files.

Run asks for confirmation and sends each line only after the previous command
is acknowledged. Navigation stays on Macros until fresh Idle feedback confirms
completion. **Stop macro** asks for confirmation before resetting the controller.
Faults cancel the remaining sequence; command errors also request feed hold.
Macros run as written: include explicit
units and distance modes where needed; changing LCD units does not rewrite them.
Each macro supports 768 characters and 32 executable lines, up to 192 characters
per line. System commands and realtime characters (`!`, `~`, `?`) are rejected.

Go-to-zero and zero-setting actions require confirmation. Go to XY Zero moves
via machine Z âˆ’5 mm before moving X and Y, then restores the previous machine Z.
Go to Z Zero moves directly to work Z0. Check this clearance against your setup.

## Files and jobs

Choose **Onboard SD** or **Controller SD** in Settings > File location. The display card lists jobs
in pages with file sizes, Preview and Run. It stores the G-code and eight cached
bitmap views together, and streams a confirmed job to grblHAL using character-counting
flow control. Controller SD lists files with Run and lets the controller execute
the job locally, without downloading previews.

A small [PC preview server](preview-server/server.py) prepares Top, Bottom,
Front, Back, Left, Right, Isometric and Perspective images while the machine is
idle. The pendant posts missing files over Wi-Fi and saves the results on its
SD card, keyed by the exact G-code content. Cached views work offline. Drag to
pan and pinch to zoom an image. Set the server address in Settings.
See [Display SD, streaming and previews](docs/display-sd-and-previews.md).

During a job, the run screen shows positions, progress and elapsed time, with
Pause/Resume and Stop below the readout. Feed and spindle overrides use the
controllerâ€™s reported values. Setup navigation is hidden until the job ends.
After Job Complete is acknowledged, the pendant returns to Jog.

Alarms and command errors appear in red full-screen pages. The header fault indicator
reopens the current fault. Descriptions come from the connected controller, including
driver and plugin codes; built-in text remains available when a description is missing.
Job Stop asks for confirmation before sending a controller reset; check position
and re-home as needed afterward. Emergency stopping remains a hardware function.

The PC renderer accepts files up to 512 MiB and 15 million segments. It handles
linear moves and IJK/R arcs in all three planes, including helical arcs, metric
or imperial units and absolute or relative positioning. Unsupported geometry
fails explicitly; Run remains a separate action. Previews do not simulate stock,
collisions or controller execution.

See the [HTTP endpoint reference](docs/http-api.md) for gCAM upload and the read-only actuator page.

## Settings and Wi-Fi

Choose metric or imperial display units, choose USB3 or configure the controller UART, and
adjust probe geometry and feeds. Configure the PC preview server address here too. Display units are converted locally; generated
motion commands and internal geometry remain in millimetres.

**Machine settings** opens the connected grblHAL controller's complete settings
list, organised by its reported groups. Find settings by name, description or
`$` number. Each editor shows the controller's description, units and limits,
with switches, option lists, axis checkboxes or an onscreen keyboard as appropriate.
Save writes one setting and checks the controller's reported value. Settings
marked as requiring a restart are identified before saving; the pendant does
not restart the controller automatically. See [Machine settings](docs/machine-settings.md).

Wi-Fi, Probe, Tools, confirmations and controller settings use full-screen pages
with the same graphite background and dark cards as Jog.

Open Wi-Fi from Settings or the header icon. Scan, select a network and enter
its password using the onscreen keyboard. Each network row shows signal strength
and a lock for secured networks. The connection result appears on a full-screen page;
OK closes both the result and network page. Successful credentials are saved
for reconnection. The header shows the connected SSID and IP address.

## Hardware and project status

- VIEWESMART 7-inch 1024Ã—600 ESP32-P4 display with ESP32-C6 Wi-Fi coprocessor.
- Native LVGL interface, built with ESP-IDF 5.3.4 and LVGL 9.2.2.
- Controller UART: fixed TX GPIO37 / RX GPIO38; baud rate is configurable in Settings (115200 initially).
- USB3 CDC ACM host connection: tested with an RP2350 grblHAL controller (2E8A:000A). USB UART remains available for programming and development tools.
- Startup alarms open an Unlock confirmation; unlocking does not establish a homed position.
- The selected controller connection starts on boot and reconnects after USB re-enumeration. Interrupted jobs do not resume automatically.

This is firmware under development. The interface and Wi-Fi have been exercised
on the display. Physical controller motion, probing and SD transfers still need
hardware validation. RS485 and the grblHAL panel plugin are being investigated.
Settings > File location selects Onboard SD or Controller SD for both Files and
Wi-Fi gCAM uploads, and persists across reboots.
Display-SD uploads, status and duplicate-file protection have been checked on
the display; controller-SD transfers still need a connected controller.

Settings includes a persistent screen-brightness slider. The onboard RGB LED
provides a supplementary machine-status indication. See the
[interface colour roles and hardware indicators](docs/color-scheme.md).

## Build and development

With ESP-IDF 5.3.4 installed and its environment activated:

```powershell
cd firmware/viewesmart-p4
$env:IDF_TARGET='esp32p4'
idf.py build
idf.py -p COM12 app-flash
```

Replace COM12 with your displayâ€™s port. The checked-in `sdkconfig` contains the
tested board configuration. ESP-IDF downloads the managed components listed in
the dependency manifests and lock file.

Development tools capture the actual LCD framebuffer and inject taps, holds and
swipes over USB UART. Demonstration mode disconnects both controller USB and UART and
supplies sample machine feedback for UI checks.

- [Development instructions and source map](AGENTS.md)
- [Controller testing and known issues](docs/controller-testing.md)
- [LCD page benchmarks](docs/page-benchmarks.md)
- [Screenshot, touch injection and test guide](docs/device-ui-development.md)
- [UART, RS485 and panel-plugin investigation](docs/hardware-and-protocol.md)
- [Inputs, spindle/coolant and macros](docs/inputs-accessories-macros.md)
- [Display SD, streaming and cached previews](docs/display-sd-and-previews.md)

## Screenshots

Screens from the actual 1024 x 600 display. Controller feedback and controller-SD
listings use demonstration data. Display-SD examples are real card files, and
Wi-Fi uses the onboard radio.
Click a screenshot to open it at full size.

### Jog

| Features and use | Preview |
| --- | --- |
| **Jog â€” metric, incremental**<br><br>Home the machine, set the work zero and jog from one screen. Machine and work positions stay beside the per-axis controls. | [<img src="docs/screenshots/panel-jog.png" alt="Metric jog screen with axis-coloured controls" width="640">](docs/screenshots/panel-jog.png) |

### Spindle, coolant and macros

| Features and use | Preview |
| --- | --- |
| **Spindle and coolant controls**<br><br>Enter an RPM, choose CW or CCW, or switch the spindle off. Mist and flood controls share this page. | [<img src="docs/screenshots/panel-accessories.png" alt="Manual spindle and coolant controls with numeric keyboard" width="640">](docs/screenshots/panel-accessories.png) |
| **Available spindle selection**<br><br>Choose from the spindles reported by the controller. The active spindle is identified; entries not enabled in controller settings remain unavailable. | [<img src="docs/screenshots/panel-spindles.png" alt="Available spindle selection" width="640">](docs/screenshots/panel-spindles.png) |
| **Spindle activation confirmation**<br><br>Confirm the spindle to activate. Selection is available while idle with stopped-spindle feedback, and the controller is queried again afterward. | [<img src="docs/screenshots/panel-spindle-activate.png" alt="Spindle activation confirmation" width="640">](docs/screenshots/panel-spindle-activate.png) |
| **Spindle start confirmation**<br><br>Check the direction and requested RPM before starting. The entered speed must fit the controller-reported range. | [<img src="docs/screenshots/panel-spindle-confirm.png" alt="Spindle start confirmation" width="640">](docs/screenshots/panel-spindle-confirm.png) |
| **Twenty macro positions**<br><br>Keep up to twenty named sequences in a four-column grid. Tap a name to run it or the pencil to edit; Add macro stays in the header. | [<img src="docs/screenshots/panel-macros.png" alt="Macro library with Add and per-macro editing" width="640">](docs/screenshots/panel-macros.png) |
| **Add or edit a macro**<br><br>Enter a name and G-code, then choose a tile colour. Saved macros remain available after restarting the display. | [<img src="docs/screenshots/panel-macro-editor.png" alt="Macro name colour and G-code editor" width="640">](docs/screenshots/panel-macro-editor.png) |
| **Run, stop and error recovery**<br><br>Confirm the selected macro before execution. Each command waits for acknowledgement before the next is sent. | [<img src="docs/screenshots/panel-macro-confirm.png" alt="Macro run confirmation" width="640">](docs/screenshots/panel-macro-confirm.png) |
| **Delete confirmation**<br><br>Delete asks for confirmation before removing a saved macro. Cancel keeps the macro unchanged. | [<img src="docs/screenshots/panel-macro-delete.png" alt="Macro delete confirmation" width="640">](docs/screenshots/panel-macro-delete.png) |

### Actuator controls and configuration

| Features and use | Preview |
| --- | --- |
| **Buttons, position slider and presets**<br><br>Control X, Y, Z and A as separate mechanisms. Each column can use endpoint buttons, a position slider, presets or repeat motion. | [<img src="docs/screenshots/panel-actuators.png" alt="Four independent actuator controls" width="640">](docs/screenshots/panel-actuators.png) |
| **Rotary A with degree readout and angular position dial**<br><br>Rotary A uses degrees and an angular position dial. Position changes apply when the finger is released. | [<img src="docs/screenshots/panel-actuator-rotary.png" alt="Actuator controls with rotary A" width="640">](docs/screenshots/panel-actuator-rotary.png) |
| **Repeated endpoint travel with speed control**<br><br>Repeat travels between the two configured endpoints until Stop. The vertical slider sets the travel speed. | [<img src="docs/screenshots/panel-actuator-repeat.png" alt="Actuator repeat control" width="640">](docs/screenshots/panel-actuator-repeat.png) |
| **Rotary A configuration**<br><br>Set A to Linear or Rotary. Saving updates only the controller bit for A and checks its reported type before permitting movement. | [<img src="docs/screenshots/panel-actuator-rotary-config.png" alt="Rotary A configuration" width="640">](docs/screenshots/panel-actuator-rotary-config.png) |
| **Mode selection**<br><br>Choose CNC or Actuator mode. Actuator mode replaces the CNC tabs with Actuators, Diag and Settings. | [<img src="docs/screenshots/panel-settings-actuator-mode.png" alt="CNC and Actuator mode selection" width="640">](docs/screenshots/panel-settings-actuator-mode.png) |
| **Per-actuator settings**<br><br>Open the settings for each actuator independently. Labels and layout choices stay here rather than crowding the control screen. | [<img src="docs/screenshots/panel-actuator-settings.png" alt="Actuator configuration rows in Settings" width="640">](docs/screenshots/panel-actuator-settings.png) |
| **Names, endpoint labels, positions and speed**<br><br>Edit the actuator name, endpoint labels, positions and speed with the onscreen keyboard. Labels can suit doors, camera sliders or other mechanisms. | [<img src="docs/screenshots/panel-actuator-config.png" alt="Actuator settings with LVGL keyboard" width="640">](docs/screenshots/panel-actuator-config.png) |

### Jog modes, probing and confirmations

| Features and use | Preview |
| --- | --- |
| **Continuous jog**<br><br>Hold a direction to keep moving and lift the finger to stop. The distance selector is disabled in continuous mode. | [<img src="docs/screenshots/panel-jog-continuous.png" alt="Continuous jog with distance disabled" width="640">](docs/screenshots/panel-jog-continuous.png) |
| **Disconnected**<br><br>Machine controls remain disabled until the controller is connected and usable feedback is available. | [<img src="docs/screenshots/panel-jog-disconnected.png" alt="Disconnected Jog controls" width="640">](docs/screenshots/panel-jog-disconnected.png) |
| **Disabled controls during Hold**<br><br>Hold and other controller interlocks disable setup movements. Disabled arrow and Z controls remain readable. | [<img src="docs/screenshots/panel-jog-disabled.png" alt="Readable disabled jog arrows during Hold" width="640">](docs/screenshots/panel-jog-disabled.png) |
| **Inch display**<br><br>Display positions and jog increments in inches. Generated movement commands are converted to millimetres locally. | [<img src="docs/screenshots/panel-jog-inch.png" alt="Inch coordinates and jog distances" width="640">](docs/screenshots/panel-jog-inch.png) |
| **Z surface probing**<br><br>Probe the stock top using plate thickness. A quick touch and release enables the separate confirmed probe action. | [<img src="docs/screenshots/panel-probe-z.png" alt="Z surface probe" width="640">](docs/screenshots/panel-probe-z.png) |
| **X/Y surface probing**<br><br>Choose the approached surface and enter tool diameter. Only the selected work axis is zeroed, using its plate offset and tool radius. | [<img src="docs/screenshots/panel-probe-surface.png" alt="Single X surface probe" width="640">](docs/screenshots/panel-probe-surface.png) |
| **Corner probing**<br><br>Choose the stock corner and tool diameter, then verify probe contact before starting the XYZ corner sequence. | [<img src="docs/screenshots/panel-probe.png" alt="Corner probe geometry and contact check" width="640">](docs/screenshots/panel-probe.png) |
| **Numeric input**<br><br>Enter dimensions using the numeric keypad. Displayed dimensions are rounded to three decimal places. | [<img src="docs/screenshots/panel-numpad.png" alt="Dimension numeric keypad" width="640">](docs/screenshots/panel-numpad.png) |
| **Homing confirmation**<br><br>Confirm homing before the machine starts its homing cycle. Individual-axis homing is also available beside the position readout. | [<img src="docs/screenshots/panel-home-confirm.png" alt="Individual-axis homing confirmation" width="640">](docs/screenshots/panel-home-confirm.png) |
| **Zeroing confirmation**<br><br>Confirm changes to the current work zero. Per-axis zero buttons and Set Zero All are on Jog. | [<img src="docs/screenshots/panel-zero-confirm.png" alt="Set work zero confirmation" width="640">](docs/screenshots/panel-zero-confirm.png) |

### Diagnostics

| Features and use | Preview |
| --- | --- |
| **Live input LEDs**<br><br>Check limits, probes, door, E-stop and other controller inputs live. Each LED sits directly beside its label: red is active, green inactive and grey unknown. | [<img src="docs/screenshots/panel-inputs.png" alt="Controller input diagnostics with active inactive and unknown LEDs" width="640">](docs/screenshots/panel-inputs.png) |
| **Inactive input LEDs**<br><br>Inputs return to green when released. Unavailable or unknown inputs stay grey rather than implying a healthy signal. | [<img src="docs/screenshots/panel-inputs-inactive.png" alt="Controller input diagnostics with inactive and unknown LEDs" width="640">](docs/screenshots/panel-inputs-inactive.png) |
| **Unavailable signals**<br><br>Grey LEDs indicate unknown feedback. Diagnostics identifies disabled controller pin reporting without changing controller settings. | [<img src="docs/screenshots/panel-inputs-unknown.png" alt="Diagnostics with pin reporting disabled" width="640">](docs/screenshots/panel-inputs-unknown.png) |

### Settings

| Features and use | Preview |
| --- | --- |
| **Display units and Wi-Fi**<br><br>Choose display units, file location, brightness and Wi-Fi; configure probing, previews and the controller. | [<img src="docs/screenshots/panel-settings.png" alt="Settings units and network" width="640">](docs/screenshots/panel-settings.png) |
| **Probe configuration**<br><br>Edit plate dimensions, probe feed and corner inset beside a large numeric keypad. Probe-page shortcuts open the same editor with the relevant field selected. | [<img src="docs/screenshots/panel-settings-probe.png" alt="Combined probe geometry and feed editor" width="640">](docs/screenshots/panel-settings-probe.png) |
| **Probe feed and corner start inset**<br><br>Adjust the probing feed and the initial corner inset to suit the plate and setup. | [<img src="docs/screenshots/panel-settings-probe-lower.png" alt="Settings service and probe configuration rows" width="640">](docs/screenshots/panel-settings-probe-lower.png) |
| **USB controller connection**<br><br>Connect a CDC ACM grblHAL controller to USB3. UART remains selectable for wired serial connections. | [<img src="docs/screenshots/panel-settings-usb.png" alt="USB3 controller connected" width="640">](docs/screenshots/panel-settings-usb.png) |
| **UART controller connection**<br><br>TX37 and RX38 are fixed for the hardware. Set the controller baud rate here. | [<img src="docs/screenshots/panel-settings-uart.png" alt="UART connection and baud setting" width="640">](docs/screenshots/panel-settings-uart.png) |
| **Inch display setting**<br><br>Switch the LCD measurement system to inches. Internal geometry and generated motion remain in millimetres. | [<img src="docs/screenshots/panel-settings-inch.png" alt="Inch units selected in settings" width="640">](docs/screenshots/panel-settings-inch.png) |

### grblHAL machine settings

| Features and use | Preview |
| --- | --- |
| **Groups and current values**<br><br>Browse the connected controller settings by their reported groups. Search by name, description or setting number. | [<img src="docs/screenshots/panel-machine-settings.png" alt="Grouped grblHAL settings" width="640">](docs/screenshots/panel-machine-settings.png) |
| **Find a setting**<br><br>Search by name, description or setting number using the onscreen keyboard. | [<img src="docs/screenshots/panel-machine-setting-search.png" alt="Search controller settings" width="640">](docs/screenshots/panel-machine-setting-search.png) |
| **Homing group**<br><br>Select a group to focus on related settings, such as the controller homing configuration. | [<img src="docs/screenshots/panel-machine-settings-homing.png" alt="Controller homing settings" width="640">](docs/screenshots/panel-machine-settings-homing.png) |
| **Paged settings list**<br><br>Move through the settings list without needing a fixed catalogue built into the display. | [<img src="docs/screenshots/panel-machine-settings-page2.png" alt="Further controller settings" width="640">](docs/screenshots/panel-machine-settings-page2.png) |
| **Numeric value, description and controller limits**<br><br>Read the controller description, units and limits before changing a numeric value. Save writes the setting and checks its readback. | [<img src="docs/screenshots/panel-machine-setting-number.png" alt="Numeric setting editor" width="640">](docs/screenshots/panel-machine-setting-number.png) |
| **Switches and option lists**<br><br>Boolean settings use a switch instead of a numeric entry. | [<img src="docs/screenshots/panel-machine-setting-switch.png" alt="Boolean setting editor" width="640">](docs/screenshots/panel-machine-setting-switch.png) |
| **Controller option list**<br><br>Choose an option using the names supplied by the controller. | [<img src="docs/screenshots/panel-machine-setting-options.png" alt="Controller option list" width="640">](docs/screenshots/panel-machine-setting-options.png) |
| **Bitfields and axis masks**<br><br>Set individual flags using checkboxes rather than calculating a combined number. | [<img src="docs/screenshots/panel-machine-setting-mask.png" alt="Homing options" width="640">](docs/screenshots/panel-machine-setting-mask.png) |
| **Homing axis mask**<br><br>Choose the axes included in an axis-mask setting. The available choices come from controller metadata. | [<img src="docs/screenshots/panel-machine-setting-axes.png" alt="Homing axis mask" width="640">](docs/screenshots/panel-machine-setting-axes.png) |
| **Text, masked passwords and addresses**<br><br>Edit text settings with the onscreen keyboard. Changes are written only when saved. | [<img src="docs/screenshots/panel-machine-setting-text.png" alt="Controller text setting" width="640">](docs/screenshots/panel-machine-setting-text.png) |
| **Masked controller password editor**<br><br>Password settings use a masked entry field. The controller may also mask the value returned during readback. | [<img src="docs/screenshots/panel-machine-setting-password.png" alt="Masked controller password editor" width="640">](docs/screenshots/panel-machine-setting-password.png) |
| **Controller IP address editor**<br><br>Edit controller address settings in the same settings browser. | [<img src="docs/screenshots/panel-machine-setting-ip.png" alt="Controller IP address editor" width="640">](docs/screenshots/panel-machine-setting-ip.png) |

### Files and cached toolpath views

The large-file example below is `/mb_83.nc` (90,668,882 bytes). The preview
service generates eight fixed views, which the pendant caches on its SD card.
The screenshots show those cached images on the actual LCD. Thin X/Y reference
lines and a `0,0,0` marker locate the file's work origin.

| Features and use | Preview |
| --- | --- |
| **Display SD files**<br><br>Browse the display card in pages, with sizes and separate Preview and Run actions. | [<img src="docs/screenshots/panel-display-files.png" alt="Display SD files" width="640">](docs/screenshots/panel-display-files.png) |
| **Controller SD files**<br><br>Browse and run files on the controller without preview downloads. | [<img src="docs/screenshots/panel-controller-files.png" alt="Controller SD files" width="640">](docs/screenshots/panel-controller-files.png) |
| **File-start fallback**<br><br>If an image is unavailable, review the first 14 G-code lines directly from onboard SD. Headers and comments stay visible while the server is offline or preparing an image. | [<img src="docs/screenshots/panel-preview-text.png" alt="G-code file-start fallback" width="640">](docs/screenshots/panel-preview-text.png) |
| **Update previews**<br><br>Run a deliberate cache update from Files. The page shows the current filename, progress and a Cancel button. | [<img src="docs/screenshots/panel-preview-update.png" alt="Preview cache update page" width="640">](docs/screenshots/panel-preview-update.png) |
| **Isometric preview**<br><br>Inspect the cached toolpath from an isometric angle. The PC is needed only to generate new previews. | [<img src="docs/screenshots/panel-preview-isometric.png" alt="Isometric preview" width="640">](docs/screenshots/panel-preview-isometric.png) |
| **Server-generated perspective view**<br><br>The preview service exports the same image as PNG and native RGB565. X/Y reference lines pass through the work origin. | [<img src="docs/screenshots/mb83-server-perspective.png" alt="Server-generated perspective view of mb_83.nc with origin references" width="640">](docs/screenshots/mb83-server-perspective.png) |
| **Server-generated top view**<br><br>Review the XY toolpath with the file origin marked 0,0,0. These exports are generated from the large example file. | [<img src="docs/screenshots/mb83-server-top.png" alt="Server-generated top view of mb_83.nc with origin references" width="640">](docs/screenshots/mb83-server-top.png) |
| **Preview server**<br><br>Set the PC service address. It generates PNG exports and native bitmap caches for fast LCD display. | [<img src="docs/screenshots/panel-preview-server.png" alt="Preview server" width="640">](docs/screenshots/panel-preview-server.png) |
| **Run confirmation**<br><br>Confirm the selected SD file before starting it. Opening a preview does not start a job. | [<img src="docs/screenshots/panel-run-confirm.png" alt="Run confirmation" width="640">](docs/screenshots/panel-run-confirm.png) |

### Running, paused and completed jobs

| Features and use | Preview |
| --- | --- |
| **Running**<br><br>Follow positions, percentage and elapsed time while a job runs. Pause, Stop and feed or spindle overrides are available here. | [<img src="docs/screenshots/panel-job.png" alt="Job progress DRO and overrides" width="640">](docs/screenshots/panel-job.png) |
| **Paused**<br><br>Resume a paused job from the run screen. Setup and jog controls stay out of the running-job workflow. | [<img src="docs/screenshots/panel-job-paused.png" alt="Paused job with Resume control" width="640">](docs/screenshots/panel-job-paused.png) |
| **Stop confirmation**<br><br>Confirm Stop before the controller resets. Check machine position and re-home as needed afterward. | [<img src="docs/screenshots/panel-stop-confirm.png" alt="Confirm stopping a job" width="640">](docs/screenshots/panel-stop-confirm.png) |
| **Complete**<br><br>Acknowledge completion to return to Jog, ready to move the machine away and prepare the next stock. | [<img src="docs/screenshots/panel-complete.png" alt="Successful job completion" width="640">](docs/screenshots/panel-complete.png) |

### Alarms and command errors

| Features and use | Preview |
| --- | --- |
| **Startup lock**<br><br>A startup alarm opens Unlock automatically. Unlock releases the lock without establishing a homed position. | [<img src="docs/screenshots/panel-startup-alarm.png" alt="Machine locked with Unlock and Cancel" width="640">](docs/screenshots/panel-startup-alarm.png) |
| **Controller alarm**<br><br>The controller supplies the explanation, including driver and plugin codes. Clear the cause before unlocking. Critical alarms offer Reset & unlock; neither action restores a homed position. | [<img src="docs/screenshots/panel-alarm.png" alt="Controller-supplied homing alarm description" width="640">](docs/screenshots/panel-alarm.png) |
| **Command error**<br><br>Command errors appear separately with the reported error. Review the cause before acknowledging or recovering. | [<img src="docs/screenshots/panel-error.png" alt="Red unsupported-command error page" width="640">](docs/screenshots/panel-error.png) |

### Wi-Fi scan, password entry and connection results

| Features and use | Preview |
| --- | --- |
| **Scan in progress**<br><br>A progress indicator stays visible while the onboard Wi-Fi radio scans for networks. | [<img src="docs/screenshots/wifi-scanning.png" alt="Wi-Fi scan progress spinner" width="640">](docs/screenshots/wifi-scanning.png) |
| **Scanned networks**<br><br>Select a scanned network. Rows show signal strength and a lock for secured networks. | [<img src="docs/screenshots/wifi-networks.png" alt="Networks with signal strength and lock icons" width="640">](docs/screenshots/wifi-networks.png) |
| **Password entry**<br><br>Enter the password for the selected SSID using the onscreen keyboard. Charcoal function keys keep the keyboard quiet and readable. | [<img src="docs/screenshots/wifi-password.png" alt="Empty Wi-Fi password field and keyboard" width="640">](docs/screenshots/wifi-password.png) |
| **Password validation**<br><br>The entry page checks the minimum password length before attempting the connection. | [<img src="docs/screenshots/wifi-password-validation.png" alt="Minimum password length validation" width="640">](docs/screenshots/wifi-password-validation.png) |
| **Connected**<br><br>A successful connection saves credentials for automatic reconnection. The header shows the SSID and IP address. | [<img src="docs/screenshots/wifi-connected.png" alt="Wi-Fi successful connection notification" width="640">](docs/screenshots/wifi-connected.png) |

Board source and documentation:
[VIEWESMART hardware](https://github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY).
Existing component licenses are retained in their source directories.

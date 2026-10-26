# Actuator mode

Select **Settings → Mode → Actuator**. The bottom navigation becomes
**Actuators / Diag / Settings**. Selecting CNC restores Jog / Files / Macros / Diag / Settings.
The mode and each actuator's configuration are saved across restarts.

Actuator mode treats X, Y, Z and A as four separate actuators. X/Y/Z are linear;
A can be linear or rotary.
The four actuators appear as four vertical columns across the landscape screen.
Each actuator has its own name, two endpoint labels, machine-coordinate
endpoints, speed and display type. Edit these in Settings using the LVGL keyboard.

| Display type | Controls | Examples |
| --- | --- | --- |
| Buttons | Two named endpoint buttons | Open / Close, Up / Down, Oop / Toe |
| Position slider | Large vertical position slider and endpoint buttons | Camera slider, adjustable vent, positioning stage |
| Three presets | First endpoint, midpoint, second endpoint | Inspection positions, adjustable fixtures |
| Repeat | Go, Stop and a vertical speed slider | Camera sweeps, repeated actuator travel |

Each card shows a large numeric machine position and a travel bar between its
configured endpoints. Both follow controller feedback. Dragging the slider selects
a target; releasing sends one move. No movement is queued while dragging.
This first version operates one actuator at a time. Stop cancels the active jog
without resetting the controller. It is separate from the CNC Job Stop action.

### Repeating travel

Choose **Repeat** in an actuator's configuration. **Go** moves toward its second
endpoint, then repeats between both endpoints until **Stop**. Reversal requires
an acknowledged move and fresh Idle feedback at the endpoint. The current
position must be inside the configured travel before Go is enabled by the model.

The vertical speed slider selects 0–100% of the lower of the configured speed
and the controller's reported maximum rate. Releasing the slider during a run
cancels the current jog, waits for fresh Idle position feedback and continues
the same leg at the new speed. Zero stops; raising it again requires Go.
Each controller move remains at least 0.1 mm/min or deg/min.

Only one actuator repeats at a time. Stop, leaving the Actuators screen, faults,
loss of feedback, a spindle starting or lost homing disarm repetition. It does
not resume after reset, reconnection or clearing an alarm. The selected speed
starts at 50% after a display restart; the configured maximum remains saved.

## Setup and coordinates

1. Configure the controller for independent X/Y/Z/A axes, including its
   homing switches, individual-axis homing support, rates and travel limits.
2. In Settings, configure the actuator's first and second machine positions and
   speed. Equal endpoints mean unconfigured; endpoint moves stay disabled.
3. Home the actuator. The controller must report its homed-axis mask through
   `H:...,mask` or `[HOME:...:mask]` before endpoint moves are enabled.
4. Select an endpoint, preset, or slider position.

Endpoints can increase or decrease, and can be negative. The first endpoint is
0% and the second is 100%, regardless of which is numerically greater. Homing
does not necessarily produce machine zero. Use the actual machine coordinates
reported by the controller when configuring endpoints.

Motion uses `$J=G53 G90 G21 <axis><position> F<speed>`. This bypasses work offsets
and leaves the CNC work coordinate system unchanged. No automatic zeroing or
controller setting changes are performed. LCD inch conversion does not generate
G20. A-axis controls require the controller's reported `$376` rotary mask to
match the Linear/Rotary selection in A's configuration. Saving A's configuration
while connected and idle updates only A's bit in `$376`, preserving the other
rotary axes, then reads the settings back. Movement stays locked until the
reported type matches. Unchanged types do not rewrite the controller setting.
There is no automatic type write at startup or simply from opening the dialog.
Offline edits save display preferences only; connect and Save A again to apply
the chosen type to the controller.

### Rotary A

In **Settings → A → Configure**, choose **Rotary**. Positions are degrees and
speed is degrees per minute, regardless of the LCD mm/inch selection. Buttons
and presets keep their existing behaviour; **Position** uses an angular dial.
Turning the dial selects a target; lifting your finger sends one absolute move.
The numeric readout and travel bar continue showing controller feedback.

Saving Rotary configures `$376=1` for A alone, or sets bit zero of an existing
mask when other rotary axes are present. Calibrate the controller's steps/degree,
rate, acceleration, travel and homing for the chosen type; changing type does not
calibrate these settings. A type mismatch disables A movement. Angular endpoints
are bounded machine angles: the pendant does not wrap angles or choose the
shortest route across zero.

grblHAL reports rotary coordinates in degrees even with `$13=1`. The parser
keeps those angles unchanged while converting linear inch reports to mm.
See the upstream [rotary settings](https://github.com/grblHAL/core/blob/master/settings.c)
and [coordinate reporting](https://github.com/grblHAL/core/blob/master/report.c).

The fourth card stays unavailable on a three-axis controller. Movement requires
fresh, identified, idle grblHAL feedback, known machine coordinates, configured
endpoints, a homed axis and a stopped spindle. Configured speed is limited by
the controller's reported maximum rate when available. After a command is
acknowledged, another move waits for fresh Idle feedback. Alarms, resets and
disconnects invalidate the homing information.
Homing has a five-minute acknowledgement timeout while feedback remains fresh;
ordinary commands keep their shorter timeout. A homing timeout resets the controller.

Positions are the controller's calculated positions, not an independent physical
measurement. Motor stalls or manual movement can invalidate them. Powered doors
and windows also need appropriate physical limits and obstruction protection;
the touchscreen is not an obstruction sensor.

## Wi-Fi status

With Wi-Fi connected, open `http://<display-ip>:47820/actuators` for a live,
read-only status page. It uses the saved actuator names and endpoint labels.
`GET /actuators/v1/state` provides the same data as JSON, with machine positions
in mm or degrees (`unit`) and endpoint-relative percentages. `position`, `first`,
`second` and `speed` use that unit; speed is per minute. Legacy `_mm` fields are
only supplied for linear axes. Unknown positions are `null`.
The page marks simulated feedback as Preview.

Browser motion commands, MQTT discovery and Home Assistant control are not
implemented in this first version. Existing gCAM handoff routes remain separate.

## Next steps

- **Doors and windows:** an MQTT Cover entity per actuator, exposing open, close,
  stop, position and availability. Use reported positions rather than estimated
  travel durations. Home Assistant's [MQTT Cover](https://www.home-assistant.io/integrations/cover.mqtt/)
  already defines these commands and states.
- **Camera sliders:** saved positions, travel speed and optional dwell times;
  a repeatable move between two positions is more useful than a binary switch.
- **Sequences:** explicit steps such as Move X to 100, Wait, then Move Y to 100.
  A Together step should send a single coordinated multi-axis move and wait
  for completion before the next step. Independent simultaneous speeds need
  a separate design because the controller uses one shared motion planner.
- **Remote control:** share the LCD's validation, define local versus remote
  ownership and authentication, and clear pending targets on disconnect.

## Development checks

`tests/panel_actuators_test.cpp` covers four-axis parsing, machine-coordinate
commands, endpoint bounds, non-finite input, homing reports, rate limits,
feedback completion, alarms and persisted-config validation. It runs as part
of `UG TEST` alongside the existing CNC tests.

The serial development hook `UG ACTUATOR FIELD <index> <ASCII-hex>` fills an
already-open configuration textarea. Indices 0–5 are name, first label, second
label, first position, second position and speed. It does not save or move;
the normal Save button performs validation. Use UART-disconnected demo mode
for scripted touch checks.

`python tools/check-actuators.py --port COM17 --configure-demo` checks the LCD
and captures the new pages. It writes Door/Camera/Vent/Lift example settings;
use it only on a development display. Add `--no-captures` for interaction checks
without screenshot transfers.

Protocol references:
[grblHAL command reference](https://github.com/grblHAL/grblhal_docs/blob/main/content/markdown/04-Reference/01-system-commands-reference.md),
[homing feedback implementation](https://github.com/grblHAL/core/blob/master/report.c),
[jog command validation](https://github.com/grblHAL/core/blob/master/gcode.c).

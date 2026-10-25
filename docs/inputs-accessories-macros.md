# Inputs, accessories and macros

## Inputs

Settings → Inputs opens the live diagnostic screen in either application mode.
It monitors X/Y/Z/A limits, primary probe, tool setter, secondary probe, door,
E-stop, reset, hold, cycle start, motor fault/warning, optional stop, block delete,
single block, probe disconnection and the selected probe signal.

`Pn:` limits and control letters describe the current asserted inputs. They clear
when absent from the next report. With extended reporting, acquired probe letters
`I/J/K` assert and `i/j/k` release; omission retains their previous state.
`O` is probe disconnected; `P` is the currently selected probe trigger signal.
Legacy probe feedback also uses `P`. Stale/disconnected feedback is unknown. A
nonasserted input does not establish that a switch is physically installed.
If `$10` disables pin reporting, the screen identifies that condition without
changing controller settings. No extra `0x87` is added to the polling loop.

## Spindle and coolant

The Jog button opens manual accessory controls. Spindle start requires Idle,
confirmation, and known controller RPM limits (both `$30`/`$31`, or the selected
spindle report) before accepting a nonzero RPM. CW,
CCW and Off send `M3 S...`, `M4 S...` and `M5`. Coolant selection first sends
`M9`, then `M7` and/or `M8`, with an acknowledgement between commands.
The display reflects `A:S/C/F/M` and `FS:` reports; it does not infer successful
output activation merely from a button click.

**Select spindle** reads `$SPINDLESH` and displays up to eight reported spindles,
including the active spindle and unavailable entries. Activating an enabled
spindle requires Idle, confirmed stopped-spindle feedback and user confirmation.
It sends `M104Q<number>` using the reported spindle number (not the hardware ID),
then reads `$SPINDLESH` again. The active report supplies that spindle's RPM range;
starting remains disabled if its range is unknown. Unsupported discovery leaves
the existing manual controls available. Selection follows Ooznest Control's
Troubleshooting spindle controls.

## Macros

CNC mode adds a Macros tab; Actuator mode retains Actuators and Settings only.
Each macro is stored separately as `panelmacros/m0` through `m19` in NVS.
The 4-column × 5-row grid has twenty positions, with Add in its header and
Delete in each tile's editor. Save/Delete report
storage failures instead of silently discarding an edit. Existing application
and Wi-Fi preference formats are unchanged.

The complete sequence is validated before any line is queued. Blank lines,
semicolon comments and parenthesised comments are removed. Printable ASCII
G-code is accepted, with 20 macros, 768 characters per macro, 32 executable lines
and 192 characters per line. Sender/system commands and realtime characters are
not accepted. G-code errors stop the remaining queue and request feed hold
(`0x82` on grblHAL). A command acknowledgement may take up to five minutes;
timeout requests hold and cancels the queue. Acknowledgements do not
prove motion completion: macro execution remains busy until a subsequent Idle
report. The tab remains visible so its confirmed Stop action is accessible.
An interrupted macro keeps Stop available; its fault dialog offers a confirmed
reset instead of leaving the controller held with no recovery control.
After the sequence, `$G` refreshes the modal state before manual controls resume.

The pendant does not rewrite user-authored G-code to match LCD display units.
Use explicit `G21`/`G90` or the units/distance mode intended by the sequence.
Tool-change workflows and preflight checks are outside this feature.

## Development

- `main/panel_model.cpp`: input/accessory parsing and validated command dispatch.
- `main/panel_macros.h`: bounded store and complete-batch validator.
- `main/panel_tools_ui.inc`: diagnostics, accessories and macro cards/editors.
- `main/tests/panel_tools_test.cpp`: protocol/interlock/queue checks in `UG TEST`.
- `tools/check-tools.py`: isolated-device UI checks and actual LCD captures.

Run `python tools/check-tools.py --port COM17` on a development display in CNC
mode with no open dialog and an empty macro library. The test disconnects the
controller UART, temporarily creates a macro, and deletes it on success. It
restores physical touch in `finally`. A failed test may leave that temporary
macro for inspection; it never sends commands to the controller UART.

`python tools/check-macro-grid.py --port COM17` fills all twenty positions with
maximum-length temporary macros, captures the grid, reboots, verifies persistence
and execution, then deletes the test library. It also requires an empty library.
The store is allocated in PSRAM; each macro is saved separately so editing one
does not require rewriting a large NVS blob.

`UG MACRO FIELD <0|1> <ASCII-hex>` fills the open name/code editor without saving
or running it. Field 0 permits 32 characters. Field 1 permits 768 characters,
including LF/CR for multiline G-code. Use the normal UI Save and Run actions.

Protocol references: [grblHAL reporting](https://github.com/grblHAL/core/blob/master/report.c),
[report mask](https://github.com/grblHAL/core/blob/master/settings.h),
[Ooznest macros](https://github.com/Ooznest/ooznest-control/blob/main/js/features/macros.js)
and [input diagnostics](https://github.com/Ooznest/ooznest-control/blob/main/js/ui/troubleshooting.js).

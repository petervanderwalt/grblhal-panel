# Live controller test plan

Target: VIEWESMART ESP32-P4 pendant and bare Sienci RP2350 grblHAL controller, connected through display USB3. No machine is attached. Keep the PC on the display UART connector for logs and screenshots.

This plan tests controller behaviour, not motor movement or machine accuracy. It does not establish that homing, probing distances, spindle wiring or physical emergency stops work on a machine. UART, other USB controller families and controller SD need their own available hardware.

## Record before testing

Record both firmware revisions, USB VID/PID, selected transport, startup alarm, axis count and controller options. Save read-only settings, modal state and offsets for comparison after the run. Do not change controller settings to make a test pass. Use new, clearly named test files; preserve existing jobs and macros.

Keep physical touch disabled during scripts and restore it afterward. Use demo mode only for simulated cases: it must isolate both USB and UART. Live cases must use actual controller responses. Keep a bounded timestamped log of commands, acknowledgements, status transitions and failures, plus screenshots at each outcome. Avoid logging credentials.

## Test order

| Stage | Cases | Pass criteria |
| --- | --- | --- |
| 1. Connect and startup | Boot with controller present; controller absent then inserted; repeated logical disconnect/connect; reboot pendant; remove/reinsert controller cable. | Correct identity and fresh feedback; no reset loop, stale coordinates presented as current, automatic job restart or unsolicited configuration writes. Startup Alarm opens the fault page with Unlock. |
| 2. Unlock and protocol | Explicit Unlock for normal startup lock; query identity, modal state, offsets and settings. Verify the controller build selects the supported realtime feedback mode, including 0x80. | Unlock matches Ooznest Control; no unnecessary reset for a normal lock. Idle must come from the controller. No automatic $10 changes. |
| 3. Read-only data | Read all settings groups, definitions, descriptions and values; search/paginate; diagnostics, probe contact and accessories where reported. Repeat reads while status is polling. | Complete metadata without lost acknowledgements or truncated values; unsupported/unknown inputs stay unknown. No writes on page entry. |
| 4. Jog and coordinates | Small incremental X/Y/Z and diagonal commands, press/hold/release continuous jog, repeated taps; mm/inch LCD conversion; WCS selection and zero confirmations. | Correct commands, one move per incremental action, continuous cancel on release, feedback-driven DRO. Cancelled confirmations send nothing. Preserve offsets unless an explicit zero test is selected and its original offset can be restored. |
| 5. Onboard SD streaming | Tiny fixture, then many short lines, near-limit accepted line lengths, comments/blank lines, CRLF, arcs and relative/absolute changes; long file soak. | Character-counting FIFO follows Ooznest webserial.js: exact ASCII wire bytes including LF, strictly below 128 outstanding bytes, only acknowledgements release space. Realtime bytes bypass FIFO. No skipped or duplicated lines. |
| 6. Running controls | Pause, wait for Hold, Resume; feed/spindle overrides; Stop Cancel and confirmed Stop; repeat with queued motion. | UI follows reported state and overrides. Stop Cancel preserves the job; confirmed Stop resets/cancels it. Sliders do not flood the command queue. Navigation cannot expose jogging during a job. |
| 7. Completion | EOF with acknowledgements pending, final dwell/motion still active, program end message before Idle, normal completion. | Complete only after EOF, all acknowledgements and a later fresh Idle. Exactly one completion screen; OK returns to Jog. |
| 8. Expected faults | Invalid command in a tiny streamed file; homing with no switches/machine attached; stop/reset during a job; USB removal during a job. | Display the actual reported error/alarm, stop further file transmission, cancel pending work and avoid Job Complete. Homing is expected to fail, but record the reported code rather than assuming one. No repeated automatic homing/unlock. |
| 9. Recovery | Cancel/reopen fault page, explicit acknowledge/unlock, required critical-alarm reset sequence, reconnect after failure, start a fresh tiny job. | No stale job resumes. Critical alarms follow Ooznest reset, reboot wait and Unlock; regular locks use $X. Homed/position validity must not be invented by unlocking. |
| 10. Controller SD | When supported: list files/sizes, run tiny file, pause/resume/stop, completion, missing file, XMODEM upload of a uniquely named fixture. | Actual controller SD responses; same job/fault rules as streaming. No display preview download for controller SD. Confirm uploaded contents using an available readback path. |
| 11. Probe and tools | Probe quick touch/release gate, single surface and corner cancellation; no-contact probe failure; spindle/coolant commands and reported states, spindle selection if supported. | No probing until quick contact test passes; obey reported state/interlocks. Geometry and accessory command selection match Ooznest. Mark contact-dependent cases pending until an input can be operated. |
| 12. Wi-Fi and coexistence | Reconnect Wi-Fi, inspect settings/diagnostics while idle, gCAM upload to the selected SD source; browse cached previews. | Network work cannot corrupt serial acknowledgements or freeze controller feedback. Upload never auto-runs. Explicit preview preparation stays unavailable during a running job. |
| 13. Soak and restore | Repeat connect/read/tiny-job/pause/stop cycles, then compare settings/offsets/modal state against the starting record. | No reset, growing allocation loss or stuck busy state. Restore deliberate test changes; leave Idle on Jog with touch enabled and no queued motion. |

## Job fixtures

Start with a short metric, relative, low-feed program containing small moves and a dwell, with spindle/coolant off. Check current soft limits and coordinates before selecting it; do not disable protection to run it. A controller rejection is a valid observed outcome, not a reason to change settings silently.

Use separate fixtures for invalid G-code and a line too long for the sender. Validation failure must happen before any file motion. Use a dwell to make Pause/Resume observable before progressing to longer moving programs. The supplied large SD examples are for catalogue, validation and preview performance first; do not begin with an 83 MB live run.

For streaming evidence, add bounded debug counters if needed: sent/acknowledged lines, outstanding bytes and high-water mark, first failure and final Idle timestamp. Do not print every line at 115200 baud and mistake logging overhead for controller performance.

## Current evidence and remaining work

- USB3 identifies RP2350 2E8A:000A and receives real position/status reports.
- Explicit $X cleared the original startup lock. The generic startup-alarm page now has a readable title and Unlock action.
- The final live read retrieved 135 settings and 23 groups from the current controller firmware. An earlier build reported 145 settings; counts come from the controller, not a fixed pendant list.
- Model and demo UI checks cover stream window accounting, hold/error/reset handling and fresh-Idle completion. These are not live job-streaming results.
- Repeated logical USB reconnect and demo isolation passed with the retained CDC interface. The startup popup opened automatically in the UI test, and its Unlock action cleared the real controller startup lock. Live jobs, homing-failure recovery, probing and accessory outputs are pending this staged run.

For every executed case record **pass**, **fail** or **not tested**, firmware revisions, observed response and the log/capture path. Do not mark unavailable hardware cases passed from demo results.

## Source references

- Ooznest Control: js/machine/webserial.js (byte accounting), js/ui/alarms_and_errors.js (normal/critical unlock), and the corresponding jog, SD and probing handlers.
- Pendant: firmware/viewesmart-p4/main/panel_stream.cpp, panel_model.cpp, panel_runtime.cpp and panel_usb.cpp.
- Existing checks: tools/check-usb.py, tools/check-workflow.py, tools/check-display-sd.py, tools/check-machine-settings.py and tools/check-probe-surfaces.py.

## 2026-10-26 follow-up

- Automatic USB connection passed two pendant reboot cycles, including the startup alarm confirmation.
- A logged physical controller power cycle reconnected automatically (USB generation 1 to 4), returned Alarm 11, and left the LCD running. Alarm 10 during power loss is expected on this controller.
- An earlier power-cycle attempt left the port unable to open; root-port enumeration recovery was added. An LCD restart (panic reset reason 4) was also observed once; the latest full UART capture did not reproduce it. Keep the full UART logger active during further reset/soak tests to obtain a backtrace if it recurs; do not mark this intermittent restart explained.
- Before the held-jog redraw fix, a requested 60 ms Slow jog held the UI for 882 ms and moved 45.24 mm on the bare board. With the same feed preset after the fix, 60/100/150 ms requests moved 1.75/2.92/4.38 mm. Host command round-trip includes later rendering; it is not the cancel-transmission timestamp.
- The embedded timing check subsequently measured cancel transmission at 65/100/150 ms for 60/100/150 ms holds in isolated demo mode. Slow remains 25% of the moving axes' maximum rate, matching Ooznest Control. No controller settings or offsets were changed by these jog trials.
- Full embedded model tests passed, including stream window/fault/fresh-Idle completion checks. Live job streaming remains pending.

### Jog appearance follow-up

CNC controls now keep their appearance through Idle/Jog, with live positions and status updated independently. Control styling runs only when availability or the mode selector changes; Wi-Fi labels and signal styling update only when their displayed values change. Native alarm/disconnect disabling and model command guards remain in effect. Rejected busy jog taps do not queue another move or open a blocking notice.

The isolated response check also verifies unchanged jog_control_updates across idle, continuous and incremental cycles, enabled-looking Jog controls with can_setup false, and actual disabling/re-enabling for disconnect and faults. The first live repeat measured 50/60/100 ms holds at 1.46/1.75/2.62 mm; host round trips were 157/169/209 ms. These injected durations do not establish a minimum physical-finger travel; physical tap feel still needs user confirmation.

### Revised jog presets

Fast / Med / Slow now use 100% / 40% / 10%, as requested. The controller was read directly: X/Y maximum 7000 mm/min and Z 3000 mm/min, giving Slow X/Y 700 and Z 300 mm/min. These values are observations, not firmware defaults. Every moving axis must have a valid reported maximum; the old 1000 mm/min fallback has been removed. The next jog uses updated reports immediately. Earlier 25% timing/travel results above describe the previous preset, not the revised Slow speed.

The revised presets passed embedded model and real-LCD command checks, including missing/partial-rate rejection and use of a changed reported rate. Live Slow holds of 50/60/100 ms then moved 0.58/0.69/1.05 mm on this controller. Controller maximum-rate settings were not edited.

# Controller testing

## Verified coverage

Last checked 27 October 2026 on the VIEWESMART ESP32-P4 revision 1.3 and a bare
Sienci RP2350 grblHAL controller (USB3 CDC ACM, `2E8A:000A`, build `20261015`).
No motors, spindle or machine loads were attached. These results establish
protocol behaviour, not physical machine operation.

| Area | Observed result |
|---|---|
| Connection | Identification, boot auto-connect, logical reconnect and a physical controller power-cycle recovery passed. |
| Feedback | Realtime `0x80` polling and reported positions passed; no automatic controller setting writes. |
| Settings | 136 values and 23 groups read; values and work offsets preserved. Counts are controller-dependent. |
| Jogging | Incremental XYZ/diagonals, continuous release and LCD inch conversion passed. Feed presets use reported axis maximum rates. |
| Display SD | Tiny, comments/CRLF, near-limit blocks, 1,004-block streams and relative G2/G3 arcs passed with matching line/byte counts and hashes. Outstanding bytes peaked at 127 and drained to zero. |
| Soak | 10,004 lines / 90,018 bytes completed without added error, receive overflow or LCD reboot. |
| Job controls | Pause/Resume, feed/spindle overrides, cancelled and confirmed Stop, navigation lock and fresh-Idle completion passed. |
| Controller SD | Listing, XMODEM upload and small-job completion passed. Four later Stop attempts recovered; see the unresolved intermittent failure below. No byte-for-byte upload readback was performed. |
| Faults | Invalid G-code and failed homing showed controller-supplied descriptions. The controller supplied 22 alarm and 84 error descriptions. |
| Accessories | CW and Off feedback passed. CCW and coolant remain limited as described below. |
| Coexistence | Wi-Fi remained responsive during streaming; busy upload returned HTTP 409 and did not auto-run. |

## Known failures and remaining checks

- **Stop/reset recovery:** one controller-SD Stop left USB enumerated but the
  controller unresponsive, even after an LCD reboot. A controller power cycle
  restored it. Four later passes do not establish a fix.
- **Post-error recovery:** `M4 S10000` returned Error 20 with the spindle off.
  Acknowledging cleared the UI error, but the next restoration file was rejected.
  Explicit reset allowed the identical file to pass. Cause remains unresolved.
- **Coolant:** M7/M8/M9 were acknowledged but accessory feedback remained Off.
  Actual output/feedback is unverified; an acknowledgement is not an active-state report.
- **Memory diagnostic:** functional embedded checks passed, but
  `staged_external_memory_copy_and_overlapping_move` failed in the latest run.
  Overall `UG TEST` therefore failed. Memory workarounds were left unchanged.
- An earlier isolated LCD panic during power-cycle testing was not reproduced
  by later logged runs. Keep full UART logging during reset/reconnect checks;
  its cause has not been established.
- Successful homing, physical probe touch/release and probing geometry,
  hard-limit/E-stop inputs, USB removal during a job, TTL UART/RS485 and other
  USB controller families still need hardware checks.
- Soft-limit testing requires reported successful homing. `$20=1` alone does
  not prove protection is active after a failed homing attempt.

## Running checks

Confirm machine isolation before live actions. Record settings, modal state,
work offsets, positions and saved pendant preferences in a fresh run directory.
Do not change controller settings to make a test pass. Read the selected stage
before running it; `homing` deliberately fails without switches.

```powershell
python tools/check-live-serial.py --isolated-controller --stage inspect --output captures/new-run
python tools/check-live-serial.py --isolated-controller --stage streams --output captures/new-run
python tools/check-live-serial.py --isolated-controller --stage controls --output captures/new-run
python tools/check-live-serial.py --isolated-controller --stage stop --output captures/new-run
```

Start with identification/read-only checks, then small jobs, controls, faults and
recovery. Use a tiny job to verify recovery before a soak run. Never resume an
interrupted job automatically. Preserve the first fault while draining replies.
Controller-SD rows are not necessarily sorted: verify the exact fixture filename
in the Start confirmation before running it.

Test fixtures use `000-00-serial-`. Remove only those fixtures afterward, wait for
each SD rescan, and compare controller values, offsets, modal state, position and
pendant preferences against that run's baseline. Restore touch and leave Idle on
Jog with spindle/coolant off. Never restore a baseline from before subsequent
user changes.

See [device UI development](device-ui-development.md) for screenshots, injected
input, audit counters and restricted fixture cleanup, and
[page benchmarks](page-benchmarks.md) for measured LCD performance.

Detailed historical logs remain in ignored local `captures/live-serial-2026-10-27/`
and `captures/live-serial-followup/` folders. Superseded reports and plans remain
available in Git history.

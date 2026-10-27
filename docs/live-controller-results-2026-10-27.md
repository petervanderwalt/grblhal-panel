# Live controller checks — 27 October 2026

VIEWESMART ESP32-P4 revision 1.3, USB3 CDC ACM to the isolated Sienci RP2350 controller (`2E8A:000A`). Controller identity: `[VER:1.1f.20261015:]`. No motors, spindle or machine loads were attached. Results describe protocol and pendant behaviour, not physical machine accuracy.

The starting record contains all 136 controller settings, modal state and work offsets. Soft and hard limits remained enabled; no controller setting writes were used to make a test pass. Logs, before/after records and CRC-verified LCD captures are retained locally in `captures/live-serial-2026-10-27/`.

## Results

| Case | Result | Evidence |
| --- | --- | --- |
| USB identification, logical reconnect and transport isolation | Pass | `baseline.log`, `baseline/usb-raw.log` |
| Automatic connection after pendant boots | Pass | Two initial boots plus the fixed-build reboot; `boot.log`, `fixed-boot.log` |
| Realtime polling | Pass | `0x80` advances; no `?` polling, no repeated `0x87` during ordinary operation. Full reports are requested again after controller reset. |
| Settings and diagnostics | Pass | 136 values, 23 groups, unchanged values; `metadata.log`, `controller-metadata.json` |
| Incremental XYZ, diagonals, continuous release, mm/inch conversion | Pass | `jog.log`. An 80 ms continuous hold moved about 0.93 mm and stopped after release. Inch conversion generated G21 with 0.254 mm, quantised to 0.25 mm by the controller's reported 100 steps/mm. |
| Cancel Home, Zero and Go-to-zero confirmations | Pass | No commands sent; `jog-events.jsonl` |
| Tiny, comments/blank lines/CRLF and near-limit block streams | Pass | Exact normalised wire counts and FNV-1a hashes; `streams.log` |
| 1,000 short blocks | Pass | 1,004 total lines, 9,018 bytes, every line acknowledged, maximum outstanding window 127 bytes. |
| Pause, Hold, Resume, overrides and navigation lock | Pass | Real moving fixture, reported Hold/Run, both overrides changed and returned to 100%; Stop Cancel preserved the job. `controls.log` |
| Confirmed Stop | Pass | Ctrl-X cancelled the job; no Job Complete; `stop-fixed.log` |
| Overlong block | Pass | Entire file rejected before transmission: zero lines sent. `invalidfile-fixed.log` |
| Invalid G999 | Pass | Actual controller Error 20, interrupted job, explicit Stop/recovery; no Job Complete. `fault-fixed.log` |
| Homing without switches | Pass for expected failure | Actual Alarm 9, explicit Unlock returned Idle; no claim of successful homing. `homing-fixed.log` |
| Probe contact gate | Pass | Unverified contact blocked Start without transmitting a probe command; `probe-fixed.log` |
| 10,000-block soak | Pass | 10,004 lines, 90,018 bytes, FNV-1a 3838176148; exact acknowledgements; final peak window **127 bytes**, zero outstanding bytes, fresh Idle and one completion screen. 503.46 seconds including deliberate dwell execution; this is not a USB throughput benchmark. `soak-fixed.log` |
| Wi-Fi during streaming | Pass | Ping remained responsive, reported busy/accepting=false, and upload returned HTTP 409. `soak-http.json`, `soak-upload-rejected.json` |
| Controller-SD upload and completion retry | Pass | The named 18-byte non-motion fixture completed on the fixed firmware, showed Job Complete and returned to Jog. Modal/offset refresh completed. `controller-retry-run.log` |
| Physical power-cycle recovery | Pass | After the requested controller power cycle, the still-powered LCD automatically identified it, received Alarm 11 and unlocked explicitly. `power-recovery-now.log` |
| Final restoration and cleanup | Pass | All 136 reported setting values, modal state and work offsets matched the starting record. Both SD sources were cleaned of test files; Onboard SD returned to 80 files. `restore-final.log`, `live-final-cleanup.log` |

During the long run, internal free memory held at about 34 KB once streaming settled; the largest free block stayed 11,776 bytes. No additional receive overflow, controller error or LCD reboot occurred. Earlier intentionally induced errors and resets remain in the lifetime counters and must not be mistaken for soak failures.

## Fixes

- Standalone spindle start/stop took approximately ten seconds to acknowledge on this controller. The previous ten-second pendant timeout produced a false local error despite a later controller `ok`. M3/M4/M5 now have a bounded 60-second acknowledgement window; ordinary commands retain their existing timeout. Regression tests cover delayed success and eventual timeout.
- Critical alarm recovery follows Ooznest Control's explicit reset, three-second reboot wait and `$X` sequence for alarms 1, 2, 10, 17 and 20. Normal locks use `$X` without reset. Recovery is cancelled on disconnect or Stop and cannot start motion. Embedded checks cover timing, reset greeting, single unlock and cancellation. Physical critical-input validation remains separate from these tests.
- Read-only `UG AUDIT` diagnostics expose stream line/byte counts, FNV hash, maximum outstanding bytes, errors, resets, polling, memory, cached reports and settings values. They do not change controller configuration.
- Controller-SD completion now recognises `[MSG:Pgm End]`, as defined by [grblHAL messages.c](https://github.com/grblHAL/core/blob/master/messages.c), alongside the older spelling. A small non-motion SD file previously finished on the controller but left the pendant on Job indefinitely. The regression requires a later fresh Idle and covers absence of any Run report. Both SD completion paths refresh modal state and offsets. The fix passed embedded checks and the subsequent live controller-SD retry.

## Limits and follow-up

- The attempted soft-limit case was invalid after unsuccessful homing: the controller accepted the move despite `$20=1`. The move was explicitly stopped. The harness now requires reported successful XYZ homing before this case. Do not infer active soft-limit protection from the setting alone. Machine coordinates changed during homing/fault trials; position must be re-established by successful homing before machine use.
- Physical probe touch/release, successful corner/surface probing, hard-limit/E-stop input activation, physical USB removal during a job, and other USB controller families were not exercised in this run.
- Controller-SD listing and Wi-Fi → XMODEM upload passed: the new `/000-00-serial-controller.nc` was listed at 18 bytes after an upload reporting success, 18/18 bytes. No byte-for-byte download was performed. A first test-script row-order assumption selected the existing `tc.macro`; that selection was explicitly stopped and machine positions did not change. The fixture was then identified visually in its Start confirmation before being run. This revealed the completion-parser bug above. The harness must never assume controller rows are alphabetically sorted.
- After the subsequent explicit Stop, USB remained enumerated but the controller stopped replying to identification commands, including after an LCD reboot. `post-sd-reset-state.log` and `physical-recovery.log` record this unresolved software-reset recovery failure. A physical controller power cycle restored automatic connection, and the fixed controller-SD completion then passed. This does **not** establish that the original Stop/reset loss of response is fixed.
- CW spindle start, cancellation and Off passed after the timeout fix. CCW was not exercised live. M7/M8/M9 and combined coolant commands were acknowledged, but actual accessory reports stayed `A:` and coolant stayed Off. Coolant output/feedback therefore failed the expected-state assertion and remains unverified; the pendant does not invent an active state from an `ok`. See `accessory-observation.log`.
- Local cleanup passed: all ten display-SD test files were removed, the original 80-file count returned, Onboard SD was restored, and the saved pendant fields matched the baseline (including units, step, speed, probe configuration, USB selection and the existing macro count). The pendant was rebooted into normal USB mode and left on Jog with physical touch enabled. See `local-cleanup.log`.
- Final restoration passed after reconnect: all 136 controller setting values, work offsets and complete modal report matched `controller-before.json`. The host setting-write counter remained zero. The controller fixture was deleted and a refreshed listing showed only the original `tc.macro`. The two local restoration fixtures were deleted too, leaving 80 onboard files, live Idle on Jog, spindle/coolant off and touch enabled. See `controller-after.json`, `controller-retry-run.log` and `live-final-cleanup.log`.
- The restoration fixture applies the saved modal report after M2: on this controller M2 selected G1 even when the previous block selected G0. This was a test-order correction, not a controller-settings change.

The current flashed working application SHA-256 is `00bab1bc07a644ab4be6cc3970776dcaf6546562da85ae128e398bbb3904eb2e`. All embedded checks passed in `cleanup-final-selftest.log`. It is a working build based on main `1539d00`, not a new published release.

## Repeating a stage

Use the ESP-IDF Python environment with pyserial, one COM12 owner, and a freshly confirmed isolated controller:

```powershell
python tools/check-live-serial.py --isolated-controller --stage streams
python tools/check-live-serial.py --isolated-controller --stage controls
python tools/check-live-serial.py --isolated-controller --stage soak
```

Read the stage before running it. `homing` deliberately fails without switches; `critical` requires successful XYZ homing. Test fixtures use the reserved `000-00-serial-` filename prefix. A failed assertion is a failed test, not permission to alter controller settings. The harness restores physical touch in its cleanup path and attempts confirmed Stop if a test fails during an active job.

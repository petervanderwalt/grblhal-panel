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

The serial-test application SHA-256 was `00bab1bc07a644ab4be6cc3970776dcaf6546562da85ae128e398bbb3904eb2e`. All embedded checks passed in `cleanup-final-selftest.log`. That working build was based on main `1539d00`, not a published release.

## Controller-supplied fault descriptions

The follow-up build reads structured `$EA` and `$EE` reports during connection.
The real RP2350 supplied **22 alarms and 84 errors**. Descriptions are cached
before a job starts: the attempted on-demand `$EE` read was deferred while the
controller was in Hold. The final implementation sends no catalogue request
after a streamed fault.

- Embedded checks passed for custom codes, malformed records, long descriptions,
  bounded-cache overflow, reconnect clearing, unsupported enumeration and
  preservation of the first streamed error while draining later responses.
- Live G999 produced Error 20 and displayed the controller's description.
  Homing without switches produced Alarm 9 and displayed its controller-supplied
  explanation. Stop/Unlock recovery and the final 136-setting, offset and modal
  comparison passed. See `fault-descriptions-fault.log`,
  `fault-descriptions-homing.log` and `fault-descriptions-restore.log`.
- Test fixtures were removed, leaving 80 display-SD files. Manual jogging and
  zeroing resumed afterward; those user changes were left intact.
  `fault-descriptions-final.log` records the final catalogue and state.

README alarm/error images were recaptured from these actual LCD fault screens.
The current flashed application SHA-256 is
`232aa9b8a49343c6d07b8772b9148e3ba12da559d7bf5dd4500fa00901d270bc`;
`fault-descriptions-selftest.log` contains the passing embedded suite. This
follow-up does not resolve the earlier controller-SD software-reset issue or
the unverified coolant feedback.

## Page benchmark and serial follow-up

The detailed [page benchmark](page-benchmarks.md) separates construction,
layout, drawing and flush timing. The retained changes reduced median Macros
from 198.8 to 138.8 ms and Jog from 541.8 to 490.5 ms. Files remained about
610 ms; Settings and several forms still need attention. No PSRAM or
framebuffer configuration was changed.

Live follow-up records are under the ignored `captures/live-serial-followup/`
directory, with stage logs alongside it:

| Case | Result | Evidence |
|---|---|---|
| Controller-SD Stop during a non-motion job | Four passes | Uploaded the named 161-byte `000-00-serial-sd-stop-followup.nc`, checked its exact name in the LCD confirmation, then confirmed Stop. Every attempt returned fresh controller identification/status with no LCD restart or receive overflow. `live-serial-followup-run.log`, `live-serial-followup-repeat.log` |
| Display-SD streaming | Five passes | Tiny, comments/CRLF, near-limit command, 1,004-block file and relative G2/G3 return-to-start arcs. All line counts, byte counts and FNV hashes matched; outstanding bytes peaked at 127 and drained to zero. Positions returned within 0.002 mm. `live-serial-followup-streams.log` |
| Pause, Resume and overrides | Pass | Held feedback, resumed, changed/restored feed and spindle overrides, cancelled Stop without reset, and verified navigation remained locked during the job. `live-serial-followup-controls.log` |
| Confirmed streamed-job Stop | Pass | Reset was counted, the stream stopped without Job Complete, and fresh Idle returned. `live-serial-followup-stop.log` |
| CCW spindle | Rejected by controller | The actual command was `M4 S10000`; the controller returned Error 20, with its unsupported-command description. The spindle stayed off. Cancellation sent nothing. This does not validate CCW output. `live-serial-followup-ccw.log`, `live-serial-followup-ccw-result.log` |
| Commands after acknowledging CCW error | Unresolved | The UI cleared the error, but the following two-line modal-restoration file received further Error 20 responses and entered Hold. Explicit confirmed reset restored operation; standalone M2 and the identical restoration file then passed. Cause is not established. `live-serial-followup-restore.log`, `live-serial-followup-recover.log`, `live-serial-followup-restore-retry.log` |

The four controller-SD Stop passes do **not** establish a fix for the earlier
intermittent reset/recovery failure. No controller-protocol change was made
based only on these successful retries. Coolant output/feedback and the
physical-input cases listed above remain unverified.

The streaming harness now includes the small relative arc case and checks that
every successful streaming fixture preserves the starting machine position.

Cleanup restored the original **80 display-SD files** and left only the original
`tc.macro` on controller SD. All 136 setting values, the complete modal report,
work offsets and machine positions matched the fresh follow-up baseline. The
setting-write counter stayed zero, overrides were 100%, spindle/coolant were off,
and physical touch was restored. Group IDs were excluded from the value comparison:
reconnection clears group metadata until Machine settings is opened again.
See `live-serial-followup-cleanup-final.log` and
`live-serial-followup/controller-final.json`.

The embedded functional checks passed (protocol, fault catalogues, probing model,
streaming, settings, tools/macros, actuators, upload and viewer parser). The separate
`staged_external_memory_copy_and_overlapping_move` diagnostic **failed**, so the
overall `UG TEST` command reported failure. Its cause was not investigated and
the existing memory workarounds were not changed. See
`live-serial-followup-selftest.log`. A subsequent pendant reboot successfully
reconnected to the real controller with no active job or fault;
`live-serial-followup-boot.log` records that pass.

The retained application SHA-256 is
`172e10ada0fb968fa9fc40e8e7a96d987619fffa98b055bca74d80bed830bfbc`.
It is a development build, not a new published release.

### Repeat a stage

Use the ESP-IDF Python environment with pyserial, one COM12 owner, and a freshly confirmed isolated controller:

```powershell
python tools/check-live-serial.py --isolated-controller --stage streams
python tools/check-live-serial.py --isolated-controller --stage controls
python tools/check-live-serial.py --isolated-controller --stage soak
```

Read the stage before running it. `homing` deliberately fails without switches; `critical` requires successful XYZ homing. Test fixtures use the reserved `000-00-serial-` filename prefix. A failed assertion is a failed test, not permission to alter controller settings. The harness restores physical touch in its cleanup path and attempts confirmed Stop if a test fails during an active job.

# Page benchmarks

Measured on the VIEWESMART ESP32-P4 revision 1.3, LVGL 9.2.2, 1024 x 600 RGB565, one framebuffer, with the live USB RP2350 controller and 80 display-SD files. Baseline is main `fcf5f73` with identical timing instrumentation. Wi-Fi was connected and all 136 controller settings and 23 groups were loaded before each measured run.

## Results

Milliseconds, medians. Main tabs have 10 samples each; forms have 3, except Wi-Fi with 1. Worst is the largest observed sample, not a percentile guarantee.

| Screen | Samples | Before total | After total | Before build | After build | After layout | After drawing | After worst |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Files | 10 | 610.0 | 610.0 | 366.8 | 332.2 | 55.2 | 218.6 | 611.6 |
| Macros | 10 | 198.8 | 138.8 | 148.8 | 89.9 | 24.0 | 23.2 | 139.6 |
| Diag | 10 | 440.9 | 421.8 | 121.5 | 99.0 | 38.3 | 283.2 | 423.5 |
| Settings | 10 | 374.5 | 403.8 | 205.2 | 197.0 | 42.6 | 162.5 | 404.6 |
| Jog | 10 | 541.8 | 490.5 | 186.9 | 139.0 | 81.2 | 269.6 | 494.9 |
| Probe | 3 | 578.6 | 627.5 | 93.7 | 93.2 | 59.9 | 473.0 | 637.6 |
| Tools | 3 | 393.7 | 392.7 | 178.6 | 177.9 | 59.1 | 154.4 | 395.2 |
| Macro editor | 3 | 416.6 | 468.3 | 46.3 | 44.3 | 18.3 | 407.0 | 490.2 |
| Preview server | 3 | 626.2 | 629.6 | 129.9 | 127.9 | 19.4 | 481.9 | 649.5 |
| Machine settings | 3 | 452.5 | 426.0 | 123.4 | 110.2 | 104.1 | 188.7 | 448.3 |
| Probe configuration | 3 | 380.8 | 371.7 | 242.9 | 238.6 | 1.0 | 109.5 | 395.3 |
| Wi-Fi scanner | 1 | 429.0 | 425.9 | 172.9 | 49.7 | 32.9 | 342.4 | 425.9 |

## Construction breakdown

Median milliseconds for the retained variant. These phases sum to construction time; drawing follows separately.

| Tab | Restore | Delete | Base setup | Create widgets | Style | State update | Objects | Internal free KB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Files | 51.9 | 19.2 | 49.2 | 101.5 | 104.0 | 5.8 | 133 | 10.3 |
| Macros | 24.4 | 23.3 | 20.5 | 7.2 | 12.3 | 1.8 | 96 | 24.2 |
| Diag | 11.5 | 0.7 | 20.6 | 22.4 | 42.5 | 1.3 | 130 | 18.9 |
| Settings | 17.6 | 3.1 | 20.6 | 68.2 | 85.8 | 1.5 | 132 | 13.6 |
| Jog | 23.4 | 13.9 | 21.2 | 25.4 | 38.1 | 16.6 | 112 | 16.1 |

Each tab refreshed all 614,400 pixels in 12 flush callbacks. Internal free memory is sampled at construction completion, not a minimum-memory guarantee. Loading controller metadata and Wi-Fi changes allocation, so cold/warm runs should not be mixed.

## Method and interpretation

Firmware timestamps measure preparation, deletion, base setup, widget creation, styling, refresh, layout, drawing, flush callbacks and flush waits. Total ends at LVGL refresh completion, not physical LCD scanout. Drawing includes flush/wait, so these times overlap. UART round-trip and screenshot transfer are excluded. The injected touch duration is also excluded.

Files, Jog and the keyboard forms still have substantial software drawing costs. Flush callbacks were below 1 ms for the main pages and waits about 2.4 ms. These measurements do not justify changing PSRAM or framebuffer settings. Settings was slower in this run; no Settings speed-up is claimed. Small form differences are especially noisy with only three samples.

## Changes retained

- Skip clearing transient body styling immediately before deleting those widgets.
- Batch page style changes and refresh inherited styles once. The required layout pass remains; removing it broke Settings row positions and was rejected.
- Reduce the diagnostic LED shadow extent while retaining a small glow. With batched styling, a separate 10-sample run restoring the original shadows increased Diagnostics from 421.8 ms to 501.3 ms (drawing 283.2 to 364.7 ms), so the smaller shadow was retained. No framebuffer or PSRAM configuration was changed.

The strongest measured improvements were Macros and Jog. Other pages remain a follow-up rather than being described as fixed. Full page caching was not added: internal free memory was already limited with the live metadata and Wi-Fi loaded.

## Validation

All 69 final samples completed with one page/form build per action; saved pendant preferences were unchanged. The controller stayed connected with no added errors, setting writes, resets or receive overflow. Maximum sampled status age was 249 ms. This is a sampled check, not a guarantee of maximum polling latency.

Raw local records: `captures/page-perf/steady-baseline.json`, `improved.json`, `batched.json`, `final-tabs.json` and their `.context.json` files. `final-tabs.json` is the original-shadow comparison, not the retained variant. They are ignored development captures; the summarized results above are tracked. See [device UI development](device-ui-development.md#measuring-page-speed) to repeat the run.

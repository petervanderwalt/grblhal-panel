# Machine settings

Open **Settings → Machine settings** with a connected, idle grblHAL controller.
The pendant reads the controller's groups, setting definitions, descriptions
and current values. This page edits controller configuration; the surrounding
Settings page configures the pendant itself.

Select a group on the left, or use **Find** to search names, descriptions and
`$` numbers. Groups include their child groups. Scroll the setting rows and use
Previous/Next for lists longer than 20 entries. Refresh rereads the controller.

Tap a value to open its editor. The controller supplies the name, units, valid
range and available options. Supported types include switches, bitfields,
axis masks, option lists, whole numbers, decimals, text, passwords and IPv4
addresses. Password values are masked; leaving their replacement field
untouched does not overwrite them.

**Save** writes only that setting, then rereads the controller and checks the
result. Cancel leaves it unchanged. A controller error or mismatched readback
is reported rather than shown as a successful save. A reboot requirement is
shown in the editor; reboot the controller separately when appropriate.

Machine settings use the controller's reported units. The pendant's mm/inch
display preference does not convert these raw configuration values. Editing
requires an idle controller and is unavailable during a job.

## Development

The implementation follows Ooznest Control's metadata-driven settings flow:
[`grbl_settings.js`](https://github.com/Ooznest/ooznest-control/blob/main/js/machine/grbl_settings.js).
It queues `$EG`, `$ES`, `$ESH` and `$$`, waiting for each acknowledgement.
Definitions and current values are separate: the final fields in `[SETTING:]`
describe reboot/null flags, not the setting's value.

- `main/panel_settings.cpp`: bounded catalog parsing, validation and comparison.
- `main/panel_model.cpp`: idle-only queries, individual writes and readback.
- `main/panel_settings_ui.inc`: grouped browser and typed LVGL editors.
- `main/panel_settings_demo.h`: isolated controller fixtures.
- `main/tests/panel_settings_test.cpp`: portable parser and model checks.

Paths above are relative to `firmware/viewesmart-p4/`.
The catalog supports up to 1,024 settings and 128 groups. Values longer than
512 characters and unsupported metadata are not editable. Oversized incoming
reports abort the operation. Passwords are masked in the catalog and numeric
setting-write values are hidden in the development LAST command.

LVGL uses the ESP-IDF heap (`CONFIG_LV_USE_CLIB_MALLOC=y`) rather than the
original fixed 64 KiB widget pool. Render at most 20 rows per page and update
control styles only when their enabled state changes. The full-screen browser
skips hidden setup-page redraws while retaining alarm and job transitions.

`UG SETTING FIELD <ASCII-hex>` fills the currently open setting or Find field
without saving it. Save still uses the real LVGL button and the model's normal
validation and readback. Text containing legacy realtime characters is refused
unless the controller reports `$39=0`; no setting is changed to enable editing.

Use isolated demo feedback for automated UI checks. It exercises actual firmware
and LCD rendering, but does not replace verification against a real controller.

Run `python tools/check-machine-settings.py --port COM17` from the repository
root with no dialog open. It disconnects the real controller UART, exercises
groups, search, pagination and typed editors, and checks demo writes/readback.
Captures are written to `captures/machine-settings/`; inspect them before
updating the README gallery. Physical touch is restored on exit.

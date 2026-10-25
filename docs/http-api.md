# HTTP endpoints

## Pendant: port 47820

Use the display's current Wi-Fi address, for example `http://192.168.0.34:47820`.

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/actuators` | Browser dashboard showing actuator status, updated every second. |
| GET | `/actuators/v1/state` | JSON with mode, connection, faults and configured actuator positions, endpoints, labels and units. |
| GET | `/gcam/v1/ping` | gCAM compatibility discovery, readiness, destination and upload-size limit. |
| POST | `/gcam/v1/load` | Accept a JSON object containing `name` and `gcode`. |
| GET | `/gcam/v1/status` | Upload state, success, bytes, total, path and message. |
| OPTIONS | `/gcam/v1/ping`, `/gcam/v1/load`, `/gcam/v1/status` | Browser preflight for permitted gCAM origins. |

There is no root landing page or `/cnc` page. The actuator dashboard and JSON
endpoint are read-only; they do not command motion or change settings. They
remain available in CNC mode and report the current mode.

Settings > File location chooses the destination for both Files and gCAM.
This choice persists across reboots and applies from every page. **Display SD** saves the
received file directly to the pendant card, including when no controller is
connected. **Controller SD** uses the acknowledged XMODEM transfer and requires
a connected, idle controller with SD-transfer support. Discovery reports the
current destination as `display-sd` or `controller-sd`.

Both accept up to 2 MiB of decoded G-code and never start the uploaded job.
Display SD rejects existing filenames instead of overwriting them, removes
failed partial writes, and refreshes the file catalogue after a successful save.
The idle preview worker then prepares cached views using the configured server.
The discovery response uses `app: "gSender"` for gCAM compatibility and
`product: "grblHAL panel"` to identify the receiver.

Example request body:

```json
{"name":"bracket.nc","gcode":"G21 G90\nG0 X0 Y0\nM30\n"}
```

Larger Display-SD files can be copied onto the card with a reader.
`tools/check-handoff-sd.py` checks the endpoint on the physical pendant using
a small uniquely named test file and UART-disconnected demo mode.

## PC preview service: port 47821

This separate Python service renders previews; it does not control the machine.
Its address is configured in the pendant's Preview server settings.

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/health` | Service and renderer version. |
| POST | `/v3/jobs/<sha256>` | Raw G-code upload with Content-Length; SHA-256 must match the body. |
| GET | `/v3/jobs/<sha256>/manifest.json` | Pending, ready or error result. |
| GET | `/v3/jobs/<sha256>/<view>.bin` | Native RGB565 bitmap for the pendant. |
| GET | `/v3/jobs/<sha256>/<view>.png` | PNG export of the same view. |

Views are `top`, `bottom`, `front`, `back`, `left`, `right`, `isometric` and
`perspective`. See [Display SD and cached previews](display-sd-and-previews.md)
for setup, limits and cache behaviour.

Pending preview manifests report stage, percent, elapsed seconds and rendering
view number. The pendant displays these while waiting for completed images.

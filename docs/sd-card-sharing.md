# SD card sharing

WebDAV exposes the **LCD's SD card** over Wi-Fi as a file share. It is independent
of Settings > File location: choosing Controller SD for jobs does not redirect
this share to the controller.

## Connect

1. Connect the LCD to Wi-Fi.
2. At the bottom of Settings, enable **SD card sharing / WebDAV**.
3. Use the address shown there, for example `http://192.168.0.34:47820/sd/`.

Sharing is disabled on a fresh installation and the setting persists across
reboots. No account or password is required. While enabled, clients on the same
network can read and change card files; use it on a trusted local network and
turn it off when finished. The endpoint uses HTTP, not HTTPS.

On Windows, paste this into File Explorer's address bar, substituting the LCD IP:

```text
\\192.168.0.34@47820\DavWWWRoot\sd
```

To assign a free drive letter for the current login:

```powershell
net use P: http://192.168.0.34:47820/sd/ /persistent:no
```

Windows needs its WebClient service. The default Windows client can limit large
file downloads to 50,000,000 bytes (`FileSizeLimitInBytes`). That is a PC-side
limit, separate from the pendant's 512 MiB upload limit. See Microsoft's
[WebDAV client troubleshooting](https://learn.microsoft.com/en-us/troubleshoot/windows-client/networking/cannot-access-webdav-web-folder).
The firmware does not change the PC's registry or service configuration.
Native Windows mapped-drive copy, SHA-256 readback, rename and delete have been
verified on the development display, including after reboot.

## File operations

Browse folders, copy files onto or off the card, create folders, rename, move,
overwrite and delete. Uploads use bounded 256 KiB receive/write batches rather
than buffering the complete file in RAM. File and folder COPY operations are
limited to 512 MiB combined and eight nested levels. Uploads accept files up to
512 MiB. Use printable ASCII names, matching the current FAT/OEM and LCD filename
support; spaces and ordinary punctuation work. Internal `.panel*` paths are private.

Files remain ordinary SD files. A successful change refreshes the LCD catalogue;
only supported non-empty G-code/text jobs appear in its Files screen, with its
existing 512-file catalogue limit. Generating previews still requires **Update**.
Uploading never starts a job.

On the development display, a 90,668,882-byte file uploaded in 649.89 seconds
and downloaded in 224.97 seconds with a matching SHA-256 hash. Those are measured
Wi-Fi/card results, not guaranteed rates. A card reader is quicker for large jobs.

Uploads go into a temporary file. An interrupted receive discards that file and
keeps an existing destination intact. Replacement uses a recovery copy and
journal because FAT rename cannot overwrite a destination directly; pending
housekeeping is recovered before the card is exposed after restart.

The share provides up to eight exclusive write leases, with server timeouts up to
one hour, for Windows drive compatibility. Locks are temporary and clear on reboot.
Custom property editing remains protected; this is a file-transfer share rather
than a document collaboration server. Clients
must use PROPFIND Depth 0 or 1. GET supports byte ranges, and writes honour single
ETag `If-Match` and `If-None-Match` conditions.

## LCD status and job exclusion

Quick operations briefly update the status header. Longer transfers open a
full-screen card with the operation, filename, bytes and percentage (or a spinner
when the total is unknown). It returns automatically to the existing screen.
An editor already open on the LCD is not dismissed to show transfer progress.

SD access is reserved during a WebDAV operation. Jobs, jogging, probing, macros,
actuators and settings writes cannot start through the pendant until it finishes.
A running job, preview update or active preview rejects a new file operation with
HTTP **423 Locked**. Controller feedback continues independently. Transfers do
not issue controller motion, reset, homing or setting commands.

## Development checks

```powershell
python tools/check-webdav-ui.py --phase enable
python tools/check-webdav.py
python tools/check-webdav-ui.py --phase activity
python tools/check-webdav-ui.py --phase busy --isolated-controller
python tools/check-webdav-ui.py --phase toggle
python tools/check-webdav-ui.py --phase final
```

The busy check runs and explicitly stops a non-motion display-SD job; use only
with a confirmed isolated controller. The HTTP check uses the reserved
`000-00-serial-webdav` folder, verifies hashes and interrupted replacement, and
cleans up its fixtures. To exercise a larger local file:

```powershell
python tools/check-webdav.py --large "C:/path/to/job.nc"
```

`UG DAV TEST` (session `dav TEST`) checks path and property parsing without
changing files. `UG SD STATE` includes sharing enablement, active access and
transfer byte counts. Captures and measured run logs go under ignored `captures/`.

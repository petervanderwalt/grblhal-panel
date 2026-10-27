# Browser installation and releases

Open https://petervanderwalt.github.io/grblhal-panel/ in desktop Chrome or Edge.
Connect the display's **UART** socket to the PC with a data cable. Close any
serial monitors, disconnect the CNC controller and select the UART port.

Click **Connect display**, select the UART port, then **Install firmware**.
Every browser installation writes the bootloader at `0x2000`, partition table
at `0x8000` and app at `0x10000`. It does not erase the whole flash or write NVS,
internal file storage or the SD card. Replacing another firmware changes its
partition layout, so its saved settings should not be assumed compatible.

The release is for the VIEWESMART 7-inch 1024×600 ESP32-P4/C6 with 16 MB flash,
tested on early P4 revision 1.3. Other panels and later silicon are unverified.
The browser checks for an ESP32-P4 before offering a write. Full browser installation of v0.1.0-beta.3 has been verified on the development LCD: all three images passed write verification, the display booted automatically, and USB, Wi-Fi and saved preferences were retained. The C6 coprocessor
is not flashed; Wi-Fi requires compatible ESP-Hosted firmware already on it.

If connection fails, close other serial software. Hold BOOT, tap RESET, release
BOOT and try Connect again. The UART connector is separate from USB3, which is
used for the controller. Safari and Firefox do not provide the required Web Serial
support for this installer. A successful install resets the display automatically.

The page uses pinned esptool-js 0.7.0, bundled with its dependencies. Downloads
are checked against the release's SHA-256 manifest before any flash write, and
esptool verifies written images with MD5. Progress and the visible log show
what is happening. Interrupted installs can be retried through the bootloader.

## Publishing a tester build

The `Firmware release and browser installer` Actions workflow runs when a `v*`
tag is pushed. It builds with the official ESP-IDF 5.3.4 container and the tracked
configuration/dependency lock, packages separate binary regions, publishes a
GitHub prerelease, and publishes the same files for the installer. A separate Pages workflow runs
from the allowed main branch after a release and deploys its binary assets.
All releases remain marked as tester prereleases. Pages must use GitHub Actions
as its build source. To rerun, select the existing release tag in Run workflow.

```sh
git tag v0.1.0-beta.3
git push origin v0.1.0-beta.3
```

Release downloads include the three binaries, manifest, checksums, instructions
and ZIP. No merged flash image is supplied because padding across the NVS region
would replace saved settings. The ZIP includes manual esptool commands.
The Pages installer always serves the most recently deployed tagged build; the
release shown on the page identifies exactly which build it installs.

For local page checks, run `npm ci`, `npm test` and `npm run build` from
`web-flasher/`. Run `python tools/package-release.py --version VERSION`, then
copy `release/` into `web-flasher/dist/firmware/` and serve `dist/` on localhost.
Web Serial requires HTTPS or localhost. Release manifests contain no credentials
or saved device settings.

# Cerberus USB web installer

## Source and hosting

The source repository is **private**: `463N7/Cerberus-Sentinel`. The public
installer is a static HTTPS website built from `web/`. It contains the browser
interface, the selected firmware images, and required redistribution notices.
It does not require visitors to sign into GitHub or install a development toolchain.

Current publication uses **GitHub Pages** from the separate public repository
[463N7/Cerberus-Flasher](https://github.com/463N7/Cerberus-Flasher). The canonical
installer URL is **https://463n7.github.io/Cerberus-Flasher/**. Pages serves the
public repository's `main` branch from its root directory. The public repository
contains the compiled website and release files; it has separate Git history
and does not contain the private firmware source checkout.

Only the contents of `web/dist/` belong in the public deployment directory.
Never publish the checkout, `dist/` firmware package history, build logs,
schematics, `.git`, credentials, or developer notes. The public firmware binaries
remain downloadable; keeping the source repository private does not hide them.

## Privacy and saved reports

The installer saves board identifiers and installation progress in browser
storage so it can resume and check that the same board remains connected.
Installation logs and startup diagnostics stay in this browser unless **Save
report** is selected. The saved report includes board MAC addresses and
diagnostic output; keep it private when sharing troubleshooting information.
Firmware files are downloaded from the public host, but the installer does not
upload board reports or radio captures.

The firmware's local dashboard uses HTTP over its password-protected WPA2 access
point and has a separate browser password. It does not provide HTTPS encryption.
NVS settings and SD files are unencrypted at rest, so physical access to the
board or card matters. Address masks are pseudonyms, not anonymity; names and
other broadcast details can still identify devices. Cerberus does not
automatically upload captured observations. Explicit network diagnostics make
DNS/HTTPS checks against `example.com` and a time check against `pool.ntp.org`.

## Hardware flow

The installer handles the production ESP32-S3 with 16 MB flash / 2 MB quad PSRAM,
and the onboard ESP32-C5 with the conservative 2 MB firmware layout.

1. Download and validate the matched bridge, C5, and S3 images.
2. Connect to the S3 native USB loader and install the temporary bridge.
3. Reconnect to the bridge's USB CDC port and program the onboard C5.
4. Return the S3 to its ROM loader and reconnect to that port.
5. Install Sentinel on the S3, verify the writes, and check startup diagnostics.

Browsers can require a new device selection when USB identity changes. A browser
permission prompt is an operator action, even when the device reset is automated.
Keep **hold BOOT, tap RESET, release BOOT** as the recovery instruction whenever
the native S3 loader does not return. The owner reported successful testing of
the existing feature build and approved **1.0.0** on **2026-10-27**, before the
rebuilt, versioned v1 package was produced. Repeat
blank-board installation and interrupted-write recovery checks when changing
the flashing workflow or supported hardware.

The C5 remains at 115200 baud and uses 4 KiB write blocks. Its existing application
write took about 146 seconds through the bridge. Do not advertise a faster factory
cycle until it is measured on production boards. Neither stage writes eFuses.

All images are separate. There is no combined image with padding across NVS.
S3 bootloader/table/app offsets are `0x0`, `0x8000`, `0x10000`; C5 offsets are
`0x2000`, `0x8000`, `0x10000`. The C5 legacy header erase is limited to
`0x0..0xFFF`. Both chips' NVS partitions are preserved by these image writes;
a future factory-reset function must be a distinct explicit operation.

## Export a release

Start with a matched build. Ordinary packaging (`tools/build_sentinel.py --target
all --package` or `tools/package_sentinel.py`) produces a candidate. An accepted
production package requires `--release --accepted-on YYYY-MM-DD`; use the actual
owner acceptance date. The v1.0.0 package and export commands are:

```powershell
python tools/build_sentinel.py --target all
python tools/package_sentinel.py --release --accepted-on 2026-10-27 --output dist/cerberus-sentinel-1.0.0
python tools/export_web_release.py dist/cerberus-sentinel-1.0.0 --verify-only
python tools/export_web_release.py dist/cerberus-sentinel-1.0.0 --set-latest
python tools/export_web_release.py --verify-site web/public/firmware
```

Export does not compile code, access USB, alter flash, or qualify a candidate
for shipping. Its first argument is the actual package directory; substitute
the new directory when publishing later releases. The default export output is
`web/public/firmware/`.
Use `--output` for an isolated export, or `--release-id` to give a new immutable
directory a portable name. Existing release directories are never overwritten.
Without `--set-latest`, the export does not change the installer selection.

Export validates every entry in the package's `SHA256SUMS.txt`, rejects missing,
unlisted and linked files, and checks image headers, target chips, actual binary
partition tables, sector bounds, matching versions, flash profiles, and NVS
placement. It copies only nine image parts and the two Watch attribution files.
It does not copy build instructions, package history, logs, original source-file
lists, or local USB identities.

Expected protocol and C5 firmware revision are read from
`shared/cerberus_protocol.h` only when that file's SHA-256 matches the package's
source receipt. For an archived package, `--source-root` can point at its matching
source checkout. This prevents the website from pairing old firmware with new
startup expectations.

The package's status and hardware-acceptance statement remain unchanged during
export. Version 1.0.0 records the owner's successful testing of the existing
feature build and release approval on 2026-10-27, before the versioned package
was rebuilt. Selecting a later candidate does not grant it that acceptance;
record physical acceptance before promoting a new shipping version.

## Public manifest contract

`firmware/latest.json` contains:

```json
{
  "manifest": "cerberus-sentinel-1.0.0/manifest.json",
  "sha256": "SHA256_OF_EXACT_MANIFEST_BYTES"
}
```

Each immutable release manifest uses `schemaVersion: 1`, with these fields:

| Field | Meaning |
| --- | --- |
| `product`, `releaseId`, `version`, `status` | Release identity and inherited qualification status |
| `builtUtc`, `sourceSha256`, `packageManifestSha256` | Build provenance without private source files |
| `hardwareValidation` | Original package's recorded validation statement |
| `hardware` | S3 flash/PSRAM and minimum C5 flash sizes in bytes |
| `expectations.protocol`, `expectations.c5Firmware` | Attested startup protocol and C5 revision |
| `notices` | Relative paths of the two required Watch attribution files |
| `targets.bridge`, `targets.c5`, `targets.s3` | Separate chip destinations and image parts |

Each target has `chip`, `flashSizeBytes`, `flashSize`, `flashMode`,
`flashFrequency`, `baud`, `eraseRegions`, `partitions`, and exactly three `parts`.
Each part contains `role`, `path`, `offset`, `size`, `sha256`, and `md5`.
Roles are **`bootloader`**, **`partition-table`**, and **`app`**. Paths such as
`c5/app.bin` resolve relative to that release's manifest. Offsets and sizes are
JSON integers in bytes. Hashes cover the exact file bytes; MD5 supports chip-side
verification and SHA-256 checks download integrity. Hashes do not substitute for
trust in the release publisher and HTTPS origin.

The release directory also contains its own `SHA256SUMS.txt`. The latest pointer
is outside that immutable directory so selecting another reviewed release does
not modify the old release.

## Build and deployment

Use Node.js 24 and Python 3.13 or newer. Web dependencies are locked in
`web/package-lock.json`.

```powershell
python tools/export_web_release.py --verify-site web/public/firmware
```

From `web/`:

```powershell
npm ci
npm run build
npm run preview
```

Then validate the actual built files from the project root:

```powershell
python tools/export_web_release.py --verify-site web/dist/firmware
```

Publish only the verified `web/dist/` contents to the public flasher repository.
The build uses relative asset URLs for the `/Cerberus-Flasher/` project path and
can also use a custom domain. Serve the site over HTTPS; the local preview uses
the browser's localhost exception. Test USB permissions and reconnect steps in
a supported desktop Chrome or Edge browser.

### Update the existing public installer

The existing site is [Cerberus USB Installer](https://463n7.github.io/Cerberus-Flasher/).
Maintain a separate checkout of [Cerberus-Flasher](https://github.com/463N7/Cerberus-Flasher)
for publication. Its `index.html`, `assets/`, `firmware/`, `guardian.svg`, and
`notices.txt` sit at the repository root, alongside `.nojekyll`. Do not place
the website inside a second `dist/` directory.

1. Make changes in the private `Cerberus-Sentinel` checkout. For firmware changes,
   build a matched package and export it with `--set-latest`. A website-only
   change can retain the existing firmware selection.
2. Run the release verification and build above. Commit and push the source, selected public
   firmware files, lockfile, and notices to the private repository, and require
   the web CI check to pass for that commit.
3. Copy the contents of the checked **`web/dist/`** into the public flasher
   checkout's root. Alternatively, use the `cerberus-usb-installer` CI artifact
   from that exact commit as the complete static website. Preserve the public
   checkout's own Git metadata, `.nojekyll`, and any configured `CNAME`; never
   copy the private checkout, its `.git` history, or hosting credentials.
4. Verify the copied firmware from the private project root, replacing the
   placeholder below with the actual public checkout location:

   ```powershell
   python tools/export_web_release.py --verify-site "<public-checkout>/firmware"
   ```

5. Review the public checkout's changed files, commit the compiled assets, and
   push to its `main` branch. Wait for the public repository's Pages deployment
   to succeed. Pushing the **private source repository** or passing its CI does
   **not** update the live website; publishing to the public repository is a
   separate step. Keep that repository public and the firmware source private.
6. Open the public HTTPS site and confirm its `firmware/latest.json` selects the
   intended release and manifest hash. Confirm the page finishes downloading
   and checking all nine images before offering USB installation.

Make future interface edits in `web/`, not in the public generated files: the
next build replaces those files. Keep the selected release's directories
immutable and preserve required notices when changing the selection.

No cross-repository publishing secret or automation is configured. If automatic
publication is added later, keep its narrowly scoped credential in private
Actions secrets and publish only the reviewed static build. Never put a token
in the browser application or public repository.

For a future custom domain, configure it in the public flasher repository's
Pages settings and retain the resulting `CNAME` file on every update. A
`flash.<your-domain>` subdomain points by DNS CNAME to `463n7.github.io`, without
a repository path. Enable HTTPS after the domain is verified. Changing domains
starts a new browser installation session because progress is stored per origin.

`web-checks.yml` validates the selected
firmware, runs `npm ci` and `npm run build`, verifies the built
firmware tree, and saves the static website as a private CI artifact. It does not
deploy, rebuild the firmware, or automatically select a new candidate.

`firmware-build.yml` is manually dispatched and installs PlatformIO **6.1.19**.
It builds each target once to generate effective configuration/dependency files,
then runs the unchanged matched build/package gate. The ZIP and its SHA-256 file
are private Actions artifacts. A completed run does not publish firmware or
replace `latest.json`; select a reviewed package explicitly after acceptance.
Build diagnostics remain private and have a short retention period.

## Publication and attribution review

- Keep local `artifacts/`, firmware `dist/`, `.pio/`, `build/`,
  `managed_components/`, `tmp/`, dependency directories, and generated SDK
  configurations out of source publication.
- Preserve `WATCH_SIGNATURES.md` and `WATCH_THIRD_PARTY_NOTICES.md` beside the
  downloadable firmware. The exporter requires both.
- The project currently has no top-level source license. Do not describe it as
  open source or add a license on the owner's behalf. Keep existing third-party
  attributions when distributing binaries, and retain notices for browser
  dependencies in the public web build.
- Keep owner-reported hardware acceptance separate from automated build checks.
  For future revisions, record a full fresh-board run and interrupted-flash
  recovery when changing the installer or supported hardware.

`web/public/notices.txt` includes full notices for the web runtime libraries,
their flasher stubs, ESP-IDF and relevant firmware components. Regenerate it
after changing dependencies with `python tools/export_web_notices.py`, after
installing the locked npm dependencies and building the firmware. That helper
reads installed upstream license files and retrieves two pinned upstream MIT
license files; it publishes no local paths. Recheck its pinned component
versions whenever the firmware or browser dependency lockfiles change.

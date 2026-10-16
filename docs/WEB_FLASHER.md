# Cerberus USB web installer

## Source and hosting

The source repository is **private**: `463N7/Cerberus-Sentinel`. The public
installer is a static HTTPS website built from `web/`. It contains the browser
interface, the selected firmware images, and required redistribution notices.
It does not require visitors to sign into GitHub or install a development toolchain.

Current publication uses **Sites**, from a separate deployment directory outside
the firmware checkout. GitHub Pages could not be enabled for this private
repository under the current account plan. The optional Pages workflow is kept
disabled unless `ENABLE_GITHUB_PAGES=true` is explicitly configured later.

Only the contents of `web/dist/` belong in the public deployment directory.
Never publish the checkout, `dist/` firmware package history, build logs,
schematics, `.git`, credentials, or developer notes. The public firmware binaries
remain downloadable; keeping the source repository private does not hide them.

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
the native S3 loader does not return. Blank-board flashing, software USB handoff,
and interrupted-write recovery need physical validation with this web workflow.

The C5 remains at 115200 baud and uses 4 KiB write blocks. Its existing application
write took about 146 seconds through the bridge. Do not advertise a faster factory
cycle until it is measured on production boards. Neither stage writes eFuses.

All images are separate. There is no combined image with padding across NVS.
S3 bootloader/table/app offsets are `0x0`, `0x8000`, `0x10000`; C5 offsets are
`0x2000`, `0x8000`, `0x10000`. The C5 legacy header erase is limited to
`0x0..0xFFF`. Both chips' NVS partitions are preserved by these image writes;
a future factory-reset function must be a distinct explicit operation.

## Export a release

Start with a matched package produced by `tools/build_sentinel.py --target all
--package`. Export does not compile code, access USB, alter flash, or qualify a
candidate for shipping.

```powershell
python tools/export_web_release.py dist/cerberus-sentinel-VERSION-TIMESTAMP --verify-only
python tools/export_web_release.py dist/cerberus-sentinel-VERSION-TIMESTAMP --set-latest
python tools/export_web_release.py --verify-site web/public/firmware
```

The first argument is the actual package directory. Replace `VERSION-TIMESTAMP`
with the generated package name. The default output is `web/public/firmware/`.
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

The package's status remains unchanged. Selecting a `development candidate`
does not turn it into a production-qualified release. Record physical acceptance
before promoting a build as the shipping version.

## Public manifest contract

`firmware/latest.json` contains:

```json
{
  "manifest": "cerberus-sentinel-VERSION-TIMESTAMP/manifest.json",
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

## Local development and deployment

Use Node.js 24 and Python 3.13 or newer. Web dependencies are locked in
`web/package-lock.json`.

```powershell
python tools/test_export_web_release.py
python tools/test_sentinel_package.py
python tools/export_web_release.py --verify-site web/public/firmware
```

From `web/`:

```powershell
npm ci
npm test
npm run build
npm run preview
```

Then validate the actual built files from the project root:

```powershell
python tools/export_web_release.py --verify-site web/dist/firmware
```

Publish only the verified `web/dist/` contents through Sites. The build uses
relative asset URLs so it can also live beneath a project path. Serve the site
over HTTPS; the local preview uses the browser's localhost exception. Test USB
permissions and reconnect steps in a supported desktop Chrome or Edge browser.

### Update the existing public installer

The existing site is [Cerberus USB Installer](https://cerberus-flasher.handyvine9.chatgpt.site).
Its linked Sites directory is the sibling **`../cerberus-web-publish/`**; its
`.openai/hosting.json` selects **`dist`** as the public directory. Keep that
hosting association and publish updates to the existing site.

1. Make changes in the private `Cerberus-Sentinel` checkout. For firmware changes,
   build a matched package and export it with `--set-latest`. A website-only
   change can retain the existing firmware selection.
2. Run the checks and build above. Commit and push the source, selected public
   firmware files, lockfile, and notices to the private repository, and require
   the web CI check to pass for that commit.
3. Copy the contents of the checked **`web/dist/`** into
   **`../cerberus-web-publish/dist/`**. Alternatively, use the
   `cerberus-usb-installer` CI artifact from that exact commit as the complete
   static directory. Keep the deployment directory's `.openai/hosting.json`
   outside the public `dist/`; never copy the firmware checkout into staging.
4. Verify the staged firmware from the private project root:

   ```powershell
   python tools/export_web_release.py --verify-site ../cerberus-web-publish/dist/firmware
   ```

5. Publish the existing Sites project from `../cerberus-web-publish/`. Publishing
   is a separate action: pushing to GitHub or passing CI does **not** update the
   live website. Do not create a new site for each firmware release.
6. Open the public HTTPS site and confirm its `firmware/latest.json` selects the
   intended release and manifest hash. Confirm the page finishes downloading
   and checking all nine images before offering USB installation.

Make future interface edits in `web/`, not in the staged generated files: the
next build replaces those files. Keep the selected release's directories
immutable and preserve required notices when changing the selection.

On Windows, the Sites publishing helper needs Git Bash on `PATH`. For an archive
path beginning with `C:/`, set `TAR_OPTIONS=--force-local` in that publishing
process so GNU tar treats the drive letter as a local path. Pass short-lived
Sites credentials through the helper's hidden standard input; never store them
in this repository or the public directory.

`web-checks.yml` runs the package/export rejection tests, validates the selected
firmware, runs `npm ci`, `npm test`, and `npm run build`, verifies the built
firmware tree, and saves the static website as a private CI artifact. It does not
deploy, rebuild the firmware, or automatically select a new candidate.

`firmware-build.yml` is manually dispatched and installs PlatformIO **6.1.19**.
It builds each target once to generate effective configuration/dependency files,
then runs the unchanged matched build/package gate. The ZIP and its SHA-256 file
are private Actions artifacts. A completed run does not publish firmware or
replace `latest.json`; select a reviewed package explicitly after acceptance.
Build diagnostics remain private and have a short retention period.

`pages-manual.yml` is an optional manually dispatched alternative after Pages is
available and configured. It deploys only `web/dist/` using the `github-pages`
environment. The current Sites deployment is separate from this workflow.

## Publication and attribution review

- Keep local `artifacts/`, firmware `dist/`, `.pio/`, `build/`,
  `managed_components/`, `tmp/`, dependency directories, and generated SDK
  configurations out of source publication.
- Historical verification documents contain real board MAC addresses and local
  port names. Keep these records private or redact a publication copy.
- Preserve `WATCH_SIGNATURES.md` and `WATCH_THIRD_PARTY_NOTICES.md` beside the
  downloadable firmware. The exporter requires both.
- The project currently has no top-level source license. Do not describe it as
  open source or add a license on the owner's behalf. Keep existing third-party
  attributions when distributing binaries, and retain notices for browser
  dependencies in the public web build.
- Browser tests and build success do not demonstrate a real board flash. Record
  a full fresh-board run and an interrupted-flash recovery before using the
  installer as a qualified production station.

`web/public/notices.txt` includes full notices for the web runtime libraries,
their flasher stubs, ESP-IDF and relevant firmware components. Regenerate it
after changing dependencies with `python tools/export_web_notices.py`, after
installing the locked npm dependencies and building the firmware. That helper
reads installed upstream license files and retrieves two pinned upstream MIT
license files; it publishes no local paths. Recheck its pinned component
versions whenever the firmware or browser dependency lockfiles change.

GitHub workflow references: [setup-node](https://github.com/actions/setup-node),
[setup-python](https://github.com/actions/setup-python),
[upload-pages-artifact](https://github.com/actions/upload-pages-artifact), and
[deploy-pages](https://github.com/actions/deploy-pages).

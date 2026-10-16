# Build and recovery

For **0.5.0-dev**, update both the S3 and onboard C5. The C5 must report firmware
revision **31**. Watch uses bounded raw BLE advertisements, decoded broadcast
snapshots, and activity measurements with capability and generation checks.

Cerberus Sentinel is a **development candidate** based on the accepted working
Cerberus Base firmware. A successful computer build does not establish that
the new screens, production PSRAM configuration, or new workflows have been
validated on a physical production board. SDR is deferred.

## Hardware profile

| Target | Profile | Application partition |
| --- | --- | --- |
| `s3-main` | ESP32-S3, 16 MB flash, 2 MB quad PSRAM | `0x10000`, size `0x700000` (7 MiB) |
| `c5-onboard` | One onboard ESP32-C5, 2 MB flash budget, no PSRAM | `0x10000`, size `0x1F0000` (1,984 KiB) |
| `s3-c5-bridge` | Temporary S3 USB-to-C5 flashing bridge | Same S3 factory slot |

The C5 profile is a conservative firmware budget, not an assertion about the
unverified module marking. No GPS, expansion radio bank, or SDR expansion board
is required. The S3 retains the Base flash mode (DIO, 80 MHz); its PSRAM uses
quad mode. These are separate buses/settings. **Do not enable octal PSRAM:**
GPIO35–37 are used by the microSD interface.

PSRAM is exposed only through explicit `MALLOC_CAP_SPIRAM` allocations. Task
stacks, DMA and ordinary allocations retain the Base internal-memory behavior.
Missing PSRAM is tolerated at boot; features that need it must handle allocation
failure. This is not validation of an alternate board model.

The touchscreen compositor allocates two RGB565 frames in PSRAM (614,400 bytes
total) when the UI starts. It compares finished frames and transfers changed
regions through the existing internal drawing buffer. If either allocation
fails, both are released and the original direct drawing path remains usable.
The read-only console's `info` command reports `screen_buffered=yes` when the
compositor is active. Production display acceptance requires that mode on N16R2.

The battery indicator reads the onboard MAX17048 on the shared I2C bus at 0x36.
It uses a 1,768-byte static tile, refreshes only its top-right region, and has no
extra task or frame allocation. The read-only `battery` command reports its
cached gauge reading; unavailable readings appear as `--%` on screen. Neither
the gauge registers nor USB attachment are used to infer a charging state.

Watch's original artwork uses a 4,736-byte internal stripe buffer, and its
bounded history, queue, and raw-advertisement staging use static internal RAM.
No extra frame allocations or external boards are required. Watch does not
connect to discovered devices, collect their content, or persist radio identity
history. The release package includes its signature catalog and third-party notices.

The production flag leaves GPIO38 free. The display remains ST7796 on SPI2
(SCLK17, MOSI18, CS16, DC4, RST5, BL6). The onboard C5 uses SPI3 (SCLK12,
MOSI11, MISO13, CS10), READY44, EN21 and BOOT40. microSD is SDMMC 1-bit
(CLK36, CMD37, DAT035). Preserve the shared pin definitions when adding tools.

## Tools and VS Code

Open this folder in VS Code. The recommended extensions are PlatformIO IDE,
C/C++, and Serial Monitor. The ordinary build shortcut runs the task
**Sentinel: Build matched S3 + C5 + bridge**. It does not upload firmware.

The working toolchain is PlatformIO Core **6.1.19**, platform
`espressif32@7.0.1`, and ESP-IDF **6.0.1** (`framework-espidf@4.60001.0`).
The platform and framework are pinned. ESP-IDF component lock files are kept
with the project. If PlatformIO is absent, install it into your chosen Python
environment:

```powershell
python -m pip install platformio==6.1.19
```

Build one processor during development:

```powershell
python tools/build_sentinel.py --target s3
python tools/build_sentinel.py --target c5
python tools/build_sentinel.py --target bridge
```

Build a matched set and produce a development package:

```powershell
python tools/build_sentinel.py --target all --package
```

The script records logs in `artifacts/build/`, checks the actual generated
partition maps and image sizes, and writes a matched-build receipt. Source
edits or dependency resolution during a build invalidate that receipt; run
the command again once edits finish. Build outputs are under `.pio/` and
`c5_firmware/.pio/`.

To package an unchanged matched set without recompiling:

```powershell
python tools/package_sentinel.py
```

The new `dist/cerberus-sentinel-.../` directory contains separate `s3/`, `c5/`,
and `bridge/` bootloader, partition and application images; actual flash
offsets; SHA-256 checksums; a manifest with the build-input hashes and matched
versions; and these instructions. The tool refuses changed sources, changed
images, a mismatched version, wrong memory profiles, invalid partition MD5,
overlapping flash regions, or an oversized application. Existing output
directories are never overwritten. The package is not an automatic installer.

## Host checks and screen previews

Install the host C compiler used by the checks, then run all host check groups:

```powershell
python -m pip install ziglang==0.14.1
python tools/test_sentinel.py
```

The checks cover protocol vectors, lesson navigation/progress/storage failures,
atomic baseline comparisons, web resource cleanup/retry, package corruption and
staleness gates, and screen/hit-area bounds. The display checks render real C
with the device font to PNGs in `artifacts/sentinel-dashboard/` and
`artifacts/sentinel-screens/`. NVS, SPI, display, and ESP resource services are
simulated; these checks do not establish electrical or live-radio behavior.

Watch 0.5 requires the matched C5 firmware revision 31 for independent Remote ID /
Pwnagotchi capture and Bluetooth activity measurements. Its C5 profile enables
passive BLE extended scans on 1M and coded PHYs, with a 255-byte receive bound;
periodic synchronization and periodic advertising remain disabled. An older
C5 can provide existing advertisement hints, with limited coverage shown in Watch.

## USB flashing and recovery

First select the actual serial port. The examples use `COMx` as a placeholder.
Neither build nor packaging commands access a device.

For normal S3 firmware:

```powershell
python -m platformio run -e s3-main -t upload --upload-port COMx
```

For the onboard C5, use the existing through-S3 bridge workflow, then restore
the S3 application:

```powershell
powershell -ExecutionPolicy Bypass -File tools/flash_c5_through_s3.ps1 -Port COMx
```

The bridge temporarily occupies the S3 application slot. Its USB CDC endpoint
enumerates separately from normal USB Serial/JTAG. The helper follows the
original USB connection and S3 serial number instead of assuming a fixed COM
port. After the C5 write, a 1200-baud CDC request returns the S3 to its ROM
loader; the helper finds it again and restores the touchscreen application.
This request does not change the C5 UART speed. If automatic recovery fails,
use the board's BOOT + RESET procedure. The bridge restores the READY
wire to UART and controls the C5 boot pins. The flasher validates the C5 slot,
uses its generated offsets, writes/verifies the C5, then restores `s3-main`.
It adjusts the local C5 esptool target to the Base's 4 KiB write block size,
preserving a backup of that tool file. The selected C5 app slot is erased as
part of the explicit flash operation.

On the first board flash, USB RTS reset left the S3 in its download loader.
An esptool watchdog reset started Sentinel successfully. The helper now ends
with that reset after restoring the application. The complete revised helper
has not yet been exercised end to end; USB handoff may still require physical
BOOT + RESET. If a successfully written S3 remains in the loader, this command
reads its volatile boot register and then performs the verified reset:

```powershell
python "$env:USERPROFILE/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32s3 --port COMx --before default_reset --after watchdog_reset read_mem 0x6000812C
```

Validate C5 images without touching hardware or modifying esptool:

```powershell
powershell -ExecutionPolicy Bypass -File tools/flash_c5_through_s3.ps1 -SkipC5Build -ValidateOnly
```

Actual bootloader offsets differ: S3 `0x0`, C5 `0x2000`; both use a partition
table at `0x8000` and factory application at `0x10000`. The packaged metadata
is the authority for each build. Never send a C5 image to the S3 or vice versa.

Both production maps currently have **one factory application**. There is
no OTA rollback slot and no automatic dual-processor update/rollback system.
Recover through the USB/ROM bootloader and reinstall the matched S3/C5 pair.
Existing NVS can contain user settings; build/package tools do not export it.

## Physical acceptance still required

Before treating a candidate as a shipping release, exercise the production
board: startup and PSRAM detection, display/touch alignment, each radio tool,
repeated mode changes, lesson progress/reboot, baseline persistence, SD
mount/removal/export, USB ownership transfer, local web start/stop, and
S3/C5 interrupted-flash recovery. Run extended operation with the actual
power supply and record heap minima. Preserve the known working Base images
as the recovery reference. SDR is a later milestone after this core is stable.

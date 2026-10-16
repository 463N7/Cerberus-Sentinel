# Flashing the ESP32-C5 Through the ESP32-S3

Use this from the Cerberus project root:

```powershell
.\tools\flash_c5_through_s3.ps1 -Port COMx
```

Validate the already-built images and partition bounds without accessing a
device:

```powershell
.\tools\flash_c5_through_s3.ps1 -SkipC5Build -ValidateOnly
```

What it does:

- Builds the onboard C5 base firmware.
- Loads the S3 bridge firmware.
- Puts the C5 into bootloader mode through the S3 control pins.
- Validates the 2 MB partition map and confirms the app image fits.
- Erases the complete C5 app slot before writing the app image.
- Flashes the C5 using the PlatformIO-generated C5 offsets.
- Verifies the C5 flash contents after writing.
- Restores the normal S3 base firmware.

Replace `COMx` with the S3 native USB Serial/JTAG port. The helper identifies
that board's USB location and serial number, follows the temporary CDC bridge
onto its new COM port, and requests S3 ROM recovery at 1200 baud after the C5
write. It then restores Sentinel on the original board. The C5 UART remains
at 115200 baud throughout. Physical BOOT + RESET is the fallback if USB
recovery fails. The 1200-baud handoff requires the updated Sentinel bridge.

The bridge uses both S3/C5 UART wires. In normal firmware, status and feature
data use SPI while the C5-to-S3 UART wire is repurposed as the active-high SPI
READY handshake. Loading the bridge restores GPIO44 to UART RX before the C5
is reset into its ROM bootloader.

Known-good C5 offsets:

```text
0x2000  bootloader.bin
0x8000  partitions.bin
0x10000 firmware.bin
```

The dedicated C5 partition map preserves 24 KiB of NVS and 4 KiB of PHY
calibration data, then assigns the remaining 1,984 KiB (`0x1F0000`) to the
factory application. The flasher reads this size from `partitions_2mb.csv`,
checks that no partition crosses the 2 MB boundary, rejects stale generated
partition images, and uses the validated factory slot as its erase range.

The C5 flash path uses 4 KB esptool write blocks because the S3 UART bridge was not reliable with the default 16 KB ESP32-C5 stub blocks.

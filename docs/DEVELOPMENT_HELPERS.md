# Development console and bridge helpers

These helpers preserve the console and recovery tooling used during integration.
They require an explicitly chosen device when run. Packaging and syntax checks of
the helpers do not access hardware; no helper was executed against a device while
preparing this contribution. See the [current checkpoint](CONTRIBUTION_REV81.md)
and [bridge protocol](C5_USB_BRIDGE.md) before using them.

Use Python 3.10 or newer. The console and handoff helpers require `pyserial`
(`python -m pip install pyserial`). The esptool wrapper uses PlatformIO's installed
`tool-esptoolpy` package as described below. Port names such as `COM7` in examples
are placeholders: select the actual endpoint and recorded device identity.

## Bounded console capture

`tools/device_test_console.py` opens the required `--port` with DTR and RTS
disabled, captures serial output for `--seconds` (greater than zero and at most
30), and writes the required `--output` file. The baud rate defaults to 115200.
It sends no command unless an explicit `--command` is supplied; each command is
sent with a newline and can change device state according to that command.
Existing output files are overwritten.

```powershell
python tools/device_test_console.py --port COM7 --seconds 3 --output artifacts/console.txt
```

Create the output directory first. Optional `--expected-bridge-mac` checks the
captured `CERBERUS_BRIDGE_V1` announcement against the supplied S3 MAC. This check
occurs after capture and any requested commands; it is a recorded identity check,
not a precondition on command transmission. Console files may contain device or
network observations and should remain in ignored local artifact directories.

## Return the temporary bridge to the S3 ROM loader

`tools/bridge_return_to_s3.py` requires both `--port` and `--mac`. Before touching
the port it verifies the bridge's USB VID/PID and serial identity, records its USB
location, then performs the documented 1200-baud handoff. It waits up to 25 seconds
for the matching S3 ROM endpoint at the same USB location. This deliberately
changes the device from the temporary C5 bridge into S3 programming mode; it does
not flash an image or start the normal application. Replace both example values
with the verified bridge endpoint and S3 MAC.

```powershell
python tools/bridge_return_to_s3.py --port COM7 --mac aa:bb:cc:dd:ee:ff
```

If the matching ROM endpoint does not return, the helper reports that physical S3
BOOT and RESET recovery is needed. It does not fall back to another attached board.

## C5 esptool block-size wrapper

`tools/cerberus_esptool.py` loads esptool from
`~/.platformio/packages/tool-esptoolpy`, sets the ESP32-C5 ROM RAM block and stub
flash-write block to 4096 bytes in the running Python process, then delegates the
supplied arguments to esptool. The required package must be installed and importable
with its dependencies in the selected Python environment. It does not edit the
installed esptool files.

```powershell
python tools/cerberus_esptool.py --help
```

This wrapper changes block sizing only. It does not select a board, verify a MAC,
validate an image package, or constrain which esptool operation can run. Supply an
explicit verified port and the documented C5 bridge settings when using it for
hardware operations; erase and write commands retain their ordinary effects.
Use matched images and offsets from their package, following
[C5 flashing](C5_FLASHING.md) and the [USB bridge limits](C5_USB_BRIDGE.md).

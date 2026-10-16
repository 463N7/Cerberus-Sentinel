# Restricted Cerberus Scripts

Cerberus scripts are declarative `.cbs` text files. They are loaded only from
`/CERBERUS/SCRIPTS` on the microSD card and are completely validated before the
Run button is enabled.

## Example

```text
CERBERUS-SCRIPT 1
# Lines beginning with # are comments.
TEXT Cerberus local automation is ready.
WAIT 1500
DIALOG This script can only use explicitly allowed display and timer actions.
WAIT 2000
TIMER 60
QR https://463n7.io/products/cerberus-development-board
WAIT 5000
TEXT Script complete.
END
```

Copy the example to `/CERBERUS/SCRIPTS/HELLO.CBS`, open **Scripts**, tap
**Reload**, select the file, review the validation result, and tap **Run
validated script**. Back cancels a running script immediately.

## Version 1 commands

| Command | Argument | Result |
| --- | --- | --- |
| `TEXT` | Printable text | Shows text on the script screen. |
| `DIALOG` | Printable text | Shows text in a highlighted local dialog. |
| `WAIT` | 1-5000 milliseconds | Delays the next action without blocking the UI. |
| `TIMER` | 1-86400 seconds | Starts the existing Cerberus local timer. |
| `QR` | Printable text or URL | Renders an offline QR code on the display. |
| `END` | None | Required end marker. |

The first non-comment line must be exactly `CERBERUS-SCRIPT 1`.

## Enforced limits

- Maximum file size: 2048 bytes
- Maximum actions: 24
- Maximum text argument: 180 printable ASCII characters
- Maximum individual wait: 5000 milliseconds
- Maximum total wait time: 30000 milliseconds
- Scripts are never started automatically or in the background
- Unknown commands reject the whole file before execution

The scripting API intentionally has no shell execution, arbitrary USB HID,
radio transmission, unrestricted file access, GPIO access, credential access,
password handling, firmware update, reset, or C5 control commands.

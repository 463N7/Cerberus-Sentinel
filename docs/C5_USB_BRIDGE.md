# C5 USB flash bridge

Cerberus has one USB connection, attached to the S3. The temporary
`s3-c5-bridge` image uses that connection to reach the onboard C5 bootloader.
Install the final S3 image after programming the C5.

## Host contract

| USB interface | Vendor / product | Purpose |
| --- | --- | --- |
| Native S3 USB Serial/JTAG | `303a:1001` | Program the bridge, then the final S3 firmware |
| Cerberus C5 Flash Bridge | `303a:4001` | Program the C5 over its onboard UART link |

The bridge USB serial string is the S3 base MAC address, formatted as six
lowercase hexadecimal octets separated by colons. This replaces the shared
`123456` serial used by older builds. Web Serial exposes vendor and product
IDs, but does not expose the USB serial string; a browser must not assume that
these IDs alone identify an individual board. The read-only 9600-baud query
below makes that same identity available to Web Serial. The final firmware's
`info` console command also reports `s3_mac=<base MAC>`.

The bridge automatically places the C5 in its ROM download mode at startup.
The UART link is fixed at **115200 baud, 8 data bits, no parity, 1 stop bit**.
DTR and RTS changes are ignored. No data payload is interpreted as a bridge
command. The browser checks the connected chip before sending flash data.
The established C5 flashing path uses the esptool stub, uncompressed data,
and **4096-byte flash blocks**. Do not increase the UART baud or substitute
the ordinary C5 16384-byte block setting without validating it on Cerberus.

Three CDC line-coding values are control requests; none changes the UART baud:

| Host baud setting | Action |
| --- | --- |
| `2400` | Reset C5 back into ROM download mode for a retry |
| `1200` | Leave the bridge and request the S3 ROM downloader |
| `9600` | Pause forwarding and return the carrier S3 identity over USB |
| `115200` | Normal bridge traffic |

### Identifying the carrier before programming C5

1. Remember the S3 MAC read from its ROM before installing the bridge.
2. Open the selected bridge port at 9600 baud and start reading immediately.
3. The bridge pauses UART forwarding, clears queued C5 USB output, and emits
   `CERBERUS_BRIDGE_V1 s3_mac=xx:xx:xx:xx:xx:xx` once, preceded and followed by
   CRLF. No DTR/RTS handshake or transmitted query bytes are required.
4. Read with a bounded timeout (at least two seconds), then close the port.
5. Require the returned MAC to match the previously selected S3. If it does
   not match or the reply is missing, do not program that bridge.
6. Use the C5 retry/reset sequence below before starting C5 communication.

Payload forwarding stays paused after the identity response until the host
sets 115200 baud. Host bytes received while paused are discarded. A few bytes
already in flight before the query may precede the identity line; parse its
explicit prefix and validate the full six-octet MAC. On completion, compare the
final firmware console's `s3_mac` to the same selected S3 identity.

### Retrying a C5 operation

1. Stop serial reads/writes and close the bridge transport.
2. Open that bridge port at 2400 baud, then close it.
3. Wait **at least 1000 ms**, allowing queued UART bytes to drain and the C5
   reset sequence to finish.
4. Reopen at 115200 baud and synchronize with the C5 ROM, using no host reset.
5. Confirm the chip is an ESP32-C5, start the supported stub, apply the
   4096-byte block limit, then repeat the interrupted operation.

### Returning to the S3

After C5 write and verification complete, close its loader transport, open
the bridge at 1200 baud, and close it. The bridge stops its forwarding task,
holds the C5 in reset, detaches USB, deinitializes CDC and TinyUSB, and restores
the internal PHY to USB Serial/JTAG before requesting S3 ROM download mode.
The browser must select the native S3 port again; the port may have a different
name and a new permission prompt.

The USB teardown and PHY restore address a known ESP-IDF behavior: software
restart preserves the USB controller selection. See Espressif
[issue 9826](https://github.com/espressif/esp-idf/issues/9826) and the later
[PHY switching fix](https://github.com/espressif/esp-idf/issues/15912).
The bridge builds against the installed ESP-IDF 6.0.1 API.

The owner reported successful testing of the existing firmware and flashing
workflow and approved the **1.0.0** release on **2026-10-27**. Keep this recovery
path available: hold the **S3 BOOT** button,
tap **RESET**, release BOOT, then select the native S3 USB port and install the
final S3 firmware. Earlier bridge builds repeatedly required this physical
recovery on Windows. Do not mark an installation complete until the final
S3 image has been written and verified.

The log-only diagnostic bridge does not implement the 2400-baud C5 retry or
9600-baud identity requests. Neither bridge writes eFuses or disables physical
recovery.

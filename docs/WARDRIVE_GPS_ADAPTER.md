# Optional ATGM336H location adapter foundation

The preserved Wardriving firmware configures the S3's UART2 at **9600 8N1**:
receiver TX into **S3 GPIO1** (UART RX), S3 GPIO2 as UART TX, and an optional
1 PPS input on **GPIO46**. These assignments come from the preserved
`integration/baselines/wardriving/shared/cerberus_pins.h` and its `gps.c`.
The Unified pin map does not currently allocate GPIO1, GPIO2, or GPIO46.
Physical routing and voltage still need verification on the expansion board.
The integrated adapter claims **only UART2 RX on GPIO1**; it does not drive
GPIO2 or claim GPIO46. GPIO46 is a boot strapping pin and should not be used
for PPS without board-level validation. There is no GPS receiver on the
current test device.

`s3_main/main/wardrive_gps_core.{h,c}` is original, hardware-independent NMEA
code for the main firmware. It accepts bounded UART byte chunks or complete
lines, requires a two-digit XOR checksum, parses GP/GN and other two-letter
talker `RMC`/`GGA` sentences, and validates coordinates, fix fields, date, and
time. It never stores a placeholder location. Its status is derived each time
from monotonic age:

| State | Condition | Snapshot coordinates |
| --- | --- | --- |
| `ABSENT` | No checksum-valid sentence in 10 seconds, or none yet | Withheld |
| `NO_FIX` | Recent receiver sentence, but no current valid fix | Withheld |
| `STALE` | Recent sentence, last valid fix older than 5 seconds | Withheld |
| `FRESH` | Recent sentence and a valid fix no older than 5 seconds | Exposed |

An explicit `RMC V` or `GGA` quality zero clears the current fix. A malformed
coordinate or GGA numeric field cannot refresh it. An `RMC` date and time that
pass validation supply a full UTC value; a `GGA` update retains that value only
when it reports the same UTC second within one second of the RMC. A fresh
location without a valid RMC date/time has `utc_valid=false`. The NMEA
two-digit year uses an 80 pivot (`00`–`79` map to 2000–2079, `80`–`99` to
1980–1999); a future date source should validate its century independently.

The current source also has `s3_main/main/wardrive_gps_uart.{h,c}`. During a
Wardriving session it installs UART2, allocates a bounded 4096-byte receive
buffer and a reader task, timestamps received NMEA chunks with the S3
monotonic clock, and feeds the parser under a mutex. Stop waits for the reader
to exit before deleting the UART driver. The service prewarms the S3 BLE host
before allocating the Wardriving worker and optional GPS reader, preserving
the first-use BLE heap gate. Missing GPS input leaves the receiver `ABSENT`
and does not prevent C5 Wi-Fi or S3 BLE scanning.

The Wardriving page now shows `ABSENT`, `NO FIX`, `STALE`, or `FRESH`, plus
whether UTC was validated. A UART setup or read error is shown separately.
The output remains `UNLOC###.CSV` even with a fresh fix: no coordinates are
written to it, and there is no WiGLE writer yet. Each row now labels the GPS
receiver state **at append time** and can include the latest validated RMC UTC
value. Neither field is claimed as the RF observation time; scan results can
be buffered before they are appended. A future geotagged writer
must accept an individual observation only with `FRESH`,
`coordinates_valid`, and validated `utc_valid`, with an observation-time
provenance check. PPS would not replace a valid NMEA fix and date.

The UART integration is source work after the installed rev47 test build. No
receiver, pin routing, or on-device GPS/WiGLE behavior has been accepted.

Run `tools/check_wardrive_gps_core.py` to compile the production parser and
exercise streaming chunks, checksum rejection, overflow, fix loss and age,
invalid coordinates/date/altitude, and UTC provenance. Build the S3 target to
check the UART adapter against ESP-IDF. These checks do not establish receiver
or pin acceptance.

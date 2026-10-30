# Integrated WiGLE 1.6 and unlocated Wardriving logs

## Installed rev80: observed AP/station relationships

The existing Wardriving session now stages the ordinary C5 scan's passive
DS1/DS2 data-header relationships alongside its AP and probe results. No new
menu, scan job, logging task, GPS requirement or separate evidence stream is
introduced. This extends the reachable upstream `WIFI_SCAN_AP_STA` workflow:
the pinned develop source dispatches it in
[`WiFiScan.cpp`](../../integration/marauder-develop-2026-10-29/esp32_marauder/WiFiScan.cpp)
and `apSnifferCallbackFull` retains observed AP/station pairs. Upstream
`WIFI_SCAN_STATION_WAR_DRIVE` remains a dormant constant with display/stop
guards; these changes do not establish a separate exposed station-Wardriving
mode or full parity.

**Cerberus unlocated Wardriving v4** retains all 29 v3 column names and their
meaning, then appends:

```csv
bssid,relation_ds,relation_rx_us,uplink_count,downlink_count
```

For a `wifi_relation` row, the existing `address` column is the observed station
address and `bssid` is the AP-side address from the data header. `relation_ds`
is `TO_AP` or `FROM_AP`, identifying the latest admitted callback's direction.
The existing channel is the receive channel. `rssi_dbm` describes that same
callback's transmitter: station for `TO_AP`, AP for `FROM_AP`. It is blank when
that callback had unavailable signal; an older valid value is never substituted.
AP SSID/security, BLE and requested-SSID fields remain blank. These address
headers are observational evidence, not proof of authenticated association,
device identity or payload contents.

`relation_rx_us` is the selected direction's full 64-bit C5 callback uptime in
microseconds. Zero is valid. It is separate from the S3 `uptime_ms`/`session_ms`
append clocks, UTC, and RF hardware time. Both directional counts are retained
from the completed sweep; callbacks, including repeated/retry reports, are
counted with saturation. The export selects one coherent latest tuple rather
than combining a downlink timestamp with an older station signal. The C5 bank
retains each direction's own time and signal for other consumers.

Relations always use the unlocated stream, including when GPS is enabled and
fresh. They do not increment GPS-fallback counts. WiGLE's 14-column AP/BLE
schema and routing remain unchanged. The compact v4 metadata and column header
are 509 bytes, within the production 512-byte storage gate; rows retain the
384-byte buffer. The new 28-byte sidecar shares the existing observation union,
so both host and target retain an 80-byte observation. The target session
workspace is 20,864 bytes in PSRAM, 2,248 more than rev79; its included relation
dedupe history is 776 bytes. These contained sizes are not extra allocations.

The existing session-only PSRAM workspace holds 32 staged relation rows and a
separate 32-entry dedupe history. Its key is exact BSSID, station and channel.
Signal comparisons use separate directional baselines; an AP's stronger
downlink cannot be compared with the station's uplink. A newly observed
direction, first available signal for that direction, a 3 dB signal improvement,
or the ordinary five-minute interval permits another row. Count and callback
time changes alone do not relog. Dedupe commits only after a complete accepted
SD append; failed writes remain retryable.

All AP/probe/relation reads and final peer/epoch checks finish before the first
SD write. Separate immutable progress cursors preserve the saved prefix through
Stop and drain only the remaining rows. A later accepted Start resets them,
including an 8-bit generation wrap. Unsupported older C5 firmware and unavailable
collection are explicit outcomes, distinct from a supported empty result.
Relation observation/log counters stay separate from AP and BLE counts; source
callback/drop summaries cover fully drained sweeps only. Existing expansion
planning and GPS lifecycle are preserved; expansion records still carry their
existing AP format and do not claim this onboard relation sidecar.

Production [core/formatter](../s3_main/main/wardrive_core.c) and
[service](../s3_main/main/wardrive_service.c) are exercised by the
[core checks](../tools/check_wardrive_core.py),
[capture lifecycle checks](../tools/check_wardrive_wifi_capture.py),
[routing checks](../tools/check_wardrive_log_routing.py), and
[real temporary-file storage checks](../tools/check_wardrive_storage.py).
Coverage includes directional signal/time coherence, zero/full-width times,
pair/channel separation, capacity/overflow, failed append/retry, whole-bank
rejection, Stop at every saved-prefix boundary, fresh-GPS unlocated routing and
the exact header/row limits. Existing WiGLE, BLE, GPS, expansion and Stop checks
also pass. The [rev80 receipt](../../integration/device-test-2026-10-27/rev80-flash-receipt.json)
records 79 host suites, five stable-input builds and nine verified paired writes.
Startup verifies mounted SD and idle service counters, not saved-file contents.
Physical data-frame reception, CSV contents and repeated-session acceptance
remain pending.

## Installed rev75: passive Wi-Fi probe hints

Ordinary Wardriving now stages matching probe requests from the C5's existing
passive management-frame callback while the S3's BLE scanner keeps its separate
radio role. This implements the receive-side name/OUI clue behavior in Marauder
develop `79cc6bcba1db6679baee1dab2b0c1ad5010e6363` ordinary Wardrive.
`WIFI_SCAN_STATION_WAR_DRIVE` remains a dormant upstream constant; this change
does not establish a separate station-wardriving mode or full feature parity.

At most 16 exact transmitter/requested-SSID/channel rows are kept per sweep.
Complete SSID information elements, source address, actual receive channel and
RSSI are validated before admission. The four unique upstream name clues and
27 OUI prefixes remain unauthenticated hints. Locally administered unicast
addresses can match a name, but never receive an OUI clue. No active stimulus or
association inference is added. Full scan epochs, callback arm time, coherent
chunk reads and peer rechecks prevent older or mixed banks from reaching SD.

**Cerberus unlocated Wardriving v3** preserves the previous 24 columns and adds
`requested_ssid_hex,probe_name_bits,probe_oui_clue,probe_observed_ms,probe_count`.
`WIFI_PROBE` rows leave the AP SSID/security and BLE evidence columns blank.
Requested SSIDs retain their explicit bytes, including embedded NUL, in hex;
they are not discovered AP names. Probe time is 64-bit C5 callback uptime in
milliseconds, separate from S3 append time and unrelated to UTC. Probe counts
describe accepted callbacks for the exact row, not distinct physical devices.
All probe evidence stays in this stream even with GPS enabled, and does not
increment the GPS-fallback counter. WiGLE's 14-column AP/BLE output is unchanged.

The new probe history uses 16 bounded entries inside the existing session-only
PSRAM workspace. Observation size stays unchanged because the probe sidecar
reuses the BLE evidence union. AP and probe rows are staged and fenced before
the first SD write; separate progress cursors let Stop drain the remaining
rows without replaying a saved prefix. A later accepted START resets staging.
Unavailable/older C5 collection is explicit rather than reported as zero
detections. Console probe-frame totals cover fully drained sweeps only.
No GPS or expansion backboard is needed for this path. Physical saved-file,
matching-hint, repeated Stop/restart and prolonged-session acceptance is pending.

The [rev75 receipt](../../integration/device-test-2026-10-27/rev75-flash-receipt.json)
records 54 stable-input host suites, five hardware-target builds, the matched
package and nine verified bridge/C5/S3 image writes. C5 firmware 57 is installed;
the temporary bridge was replaced by the normal S3 application. Target ELF
measurements put the C5 probe table at 1,056 bytes, the S3 session workspace at
18,616 bytes (+1,716), and static Wardriving status at 360 bytes (+24).
Total internal static growth is 32 S3 bytes and 1,056 C5 bytes. Observation
size remains 80 bytes. Startup confirms mounted SD, an online 40 MHz link,
10 S3 tasks and zero idle hint/probe counters; it is not live receive acceptance.

## Installed rev73: Flock evidence in the existing session

The main Wardriving service now copies each completed S3 BLE bank and its
28-byte-per-row Flock evidence under one scanner mutex. Current advertisement
and matched response retain separate facts, sequences and callback uptime;
an OUI clue remains typed-address context. Their specificity levels are
evaluated separately, never combined into stronger evidence or verified
device identity. Counters describe processed BLE rows across sweeps, not
unique physical devices or saved rows. A PSRAM progress cursor lets Stop drain
the remaining frozen rows without counting a processed prefix twice.

The no-GPS/fallback stream uses **Cerberus unlocated Wardriving v2**. It adds
separate ADV/RSP level, four fact/context bytes encoded as eight hex digits,
sequence and observation time, plus OUI-clue and optional SID fields. BLE
times are callback uptime modulo 2^32 milliseconds, not UTC, RF arrival time
or log receipt time. Missing source fields remain blank. An exact 41-bit
ADV/RSP/OUI key permits a changed clue to be logged; changing only timestamps
does not force a duplicate every sweep. Existing RSSI/time rules still apply.

WiGLE's 14-column schema, one-stream routing and located dedupe policy remain
unchanged. A row accepted into WiGLE does not also get an evidence CSV row;
located Flock evidence export remains an explicit gap. No new Flock menu,
scanner task or GPS requirement is added. The pinned Marauder
`BT_SCAN_FLOCK_WARDRIVE` is a dormant constant, not proof of another exposed
workflow.

The [rev73 receipt](../../integration/device-test-2026-10-27/rev73-flash-receipt.json)
records 52 host suites, five stable-input builds, the matched package, S3 READY
gate and three verified S3 writes on 2026-10-30. C5 firmware 56 remains installed
with all three image hashes unchanged from rev72; the current bridge was built
but not flashed. Wardriving's session-only PSRAM workspace is 16,900 bytes
(+3,460); its contained staging/dedupe data are not separate allocations.
Static status is 336 bytes (+16), matching the S3 static increase. Startup
mounted SD and four zero hint counters verify idle initialization only.
Saved CSV contents, classified counters, repeated Stop/restart, GPS/backboard
operation and prolonged runs still need physical acceptance.

The main firmware's [Wardriving service](../s3_main/main/wardrive_service.c)
calls the [WiGLE formatter](../s3_main/main/wardrive_wigle.c) for Wi-Fi and
BLE observations. Both are included in the
[S3 component build](../s3_main/main/CMakeLists.txt). This runtime adapter was
installed in [rev52](../../integration/device-test-2026-10-27/rev52-flash-receipt.json);
it is not an unintegrated formatter or a separate firmware image.

With GPS OFF, the session writes **unlocated** Cerberus CSV. With GPS ON,
valid fresh location/UTC and a representable AP/BLE observation allow a WiGLE
row; otherwise that observation falls back to the unlocated stream. Probe and
relationship evidence always use the unlocated stream. No GPS module
or six-C5 backboard is currently available for physical acceptance. An
installed adapter does not establish tested geotagged output.

The formatter emits `WigleWifi-1.6` and the following 14-column header, matching
the linked [WiGLE CSV format](https://api.wigle.net/csvFormat-1_6.html):

```csv
MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
```

The metadata and column headers are constants in
[wardrive_wigle.h](../s3_main/main/wardrive_wigle.h). The service writes them
when the located stream opens. The pre-header explicitly declares
`locationSource=GPS-at-result-receipt,timeSource=latest-RMC,rfTime=unavailable`.
Its release field remains `unversioned`; it does not identify the exact
installed build. The preserved separate Wardriving firmware uses older 1.4
output; this implementation uses the 1.6 column layout.

Wi-Fi rows use `Type=WIFI`; frequency is derived only from supported 2.4/5 GHz
channels, and `AuthMode` is the retained coarse security class, not an invented
cipher or key-management claim. BLE rows use `Type=BLE`, `AuthMode=[BLE]`,
channel 0 and blank frequency. A known BLE manufacturer ID is emitted
numerically; unknown manufacturer IDs and RCOIs stay blank.

The formatter consumes exactly `label_length` SSID/name bytes, validates
UTF-8, CSV-quotes the label and doubles embedded quotes. Hidden/empty labels
become `""`. Embedded NUL, control bytes and invalid UTF-8 reject the located
row so the service can retain the observation in its unlocated format.
`AltitudeMeters` is blank without a valid altitude; `AccuracyMeters` stays
blank because HDOP is dimensionless, not meters.

## Geotagged row gates

The formatter returns a rejection code and leaves the destination buffer and
length unchanged unless all these conditions hold:

1. The observation is a valid Wi-Fi or BLE record; Wi-Fi also requires a
   supported channel. The Wi-Fi-only wrapper rejects BLE input.
2. The GPS snapshot is `FRESH`, has finite in-range coordinates, and its fix
   age is at most 1,000 ms.
3. UTC came from a checksum-validated RMC sentence with a valid calendar date
   and time, and its age is at most 1,000 ms. The GPS snapshot carries a
   separate `utc_age_ms`; missing UTC is `UINT32_MAX`.
4. The caller declares either `RESULT_RECEIPT` or `RF_CALLBACK` and supplies
   its corresponding nonzero S3-monotonic timestamp. The GPS snapshot time
   must be on that clock, not precede the declared time, and be at most
   1,000 ms later. `UNKNOWN`, a missing time, a backward snapshot or excessive
   delay rejects the located row. The current runtime uses `RESULT_RECEIPT`.
5. The label and numeric fields are representable and the destination buffer
   has room. A failure leaves both destination bytes and written length unchanged.

`FirstSeen` uses the latest validated RMC UTC, dropping fractional seconds.
The runtime supplies its S3 result-handling time at `append_observation`
entry, before preparing the SD row, and samples the GPS there. This associates
the result with the receiver position when the service handles it; it does
**not** locate the original RF frame. Completed scans and queued expansion
records may be older, so motion during that delay can change the associated
position. The one-second checks bound GPS freshness and local handling delay,
not scan/queue age. An absent receiver, no fix or stale/missing UTC produces
unlocated output instead.

The formatter also supports `RF_CALLBACK` for callers that genuinely have
a translated RF timestamp. Current C5 AP results and expansion FIFO records
lack synchronized RF observation times, so that stronger runtime guarantee
still needs protocol and clock-provenance work. The C5 scan adapter also
derives SSID length with `strnlen`; preserving arbitrary raw SSID bytes end to
end still requires the original length and a policy for embedded NUL. The
formatter's byte validation cannot recover information already lost upstream.

## Runtime storage and ownership

[wardrive_service.c](../s3_main/main/wardrive_service.c) routes each observation
to one stream: `/CERBERUS/WARD/UNLOC###.CSV` or `WIGLE###.CSV`.
Separate bounded dedupe tables let an identity previously logged without GPS
qualify for its first located row when a fix becomes available. A failed
append does not commit dedupe. The tables and 384-byte row buffer live in the
session's PSRAM workspace.

The [storage implementation](../s3_main/main/storage.c) opens each stream
lazily, avoids existing CSV/TMP names, and bounds each file to 64 MiB. The
service rotates a full stream. Writes sync at 25 pending rows or two seconds;
the worker also syncs open streams periodically. A successful final close
publishes `.TMP` as `.CSV`. Short writes or failed sync/close retain the
failed temporary file instead of publishing it as a successful CSV.

A session lease blocks USB ownership even before either lazy stream opens.
Stop drains retained completed radio results, closes both streams and then
releases the lease. Mutex timeouts during close retain ownership for retry.
GPS OFF does not start a GPS UART/task. The service contains no automatic
WiGLE upload. See [integrated GPS logging](WARDRIVE_GPS_LOGGING.md) for the
routing and resource design; use the receipts below for installation status.

## Verification and remaining acceptance

Existing host fixtures cover the formatter and its production adapters:

- [check_wardrive_wigle.py](../tools/check_wardrive_wigle.py): NMEA RMC/GGA,
  freshness/UTC, both declared time sources, rejected-time/output preservation,
  SSID bytes, frequency and blank unknown measurements.
- [check_wardrive_log_routing.py](../tools/check_wardrive_log_routing.py):
  production routing, GPS loss/recovery, separate dedupe, BLE columns,
  UTF-8 fallback, append failure and rotation.
- [check_wardrive_storage.py](../tools/check_wardrive_storage.py),
  [check_wardrive_stop.py](../tools/check_wardrive_stop.py) and
  [check_wardrive_gps_optional.py](../tools/check_wardrive_gps_optional.py):
  storage failures, stream closure/lease ownership and no-GPS behavior.

The [rev52 receipt](../../integration/device-test-2026-10-27/rev52-flash-receipt.json)
records installation of the two-stream adapter and two no-GPS Start/Stop runs
without reset. Its final session recorded 52 C5 Wi-Fi sweeps, 31 S3 BLE sweeps,
111 unlocated rows and one saved file. Later
[rev54](../../integration/device-test-2026-10-27/rev54-flash-receipt.json) and
[rev55](../../integration/device-test-2026-10-27/rev55-flash-receipt.json)
receipts corroborate short onboard no-GPS runs; rev55 recorded 60 unlocated
rows, zero service errors and released BLE ownership after Stop. These are
historical, version-specific observations, not a new current-build retest.

Physical GPS/geotagged output, the six-C5 backboard, saved CSV contents and
prolonged/repeated operation on the current build remain unverified. Those
limits also prevent a complete Wardriving/upstream parity claim. This
documentation correction did not rerun tests or change firmware/device state.

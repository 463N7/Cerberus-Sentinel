# Ordinary Wi-Fi AP/client observations

Revision 81 S3 is installed with the byte-identical C5 firmware 60 retained
from paired rev80. The
[installation receipt](../../integration/device-test-2026-10-27/rev81-flash-receipt.json)
binds 79 host suites, five stable-input target builds, matched images and three
current S3 verified writes. Initial selected-AP list reception was owner-confirmed
on rev80, followed by a missing-Back report. Rev81 explicitly labels the parent
footers BACK TO AP, BACK TO CLIENTS and BACK TO SCAN, widens the Observed Clients
footer, and removes the duplicate detail-body control. Actual parent rendering
and navigation dispatch are covered together, including error and OOM exits.
There is no static RAM increase or C5/RF scheduling change. Physical Back/rescan
acceptance, detail evidence, saved-file contents and response-time acceptance
remain pending. Work pauses at this milestone at the owner's request.

The reachable Marauder develop workflow is `WIFI_SCAN_AP_STA` (49), exposed by
Scan AP/STA and the CLI. The reviewed reference is
[79cc6bc](https://github.com/justcallmekoko/ESP32Marauder/commit/79cc6bcba1db6679baee1dab2b0c1ad5010e6363):
`MenuFunctions.cpp:5238`, `CommandLine.cpp:987`, `WiFiScan.cpp:2850` and
`RunAPScan` at 4387. Its data callback associates header addresses with APs.
The dormant station scan/wardrive symbols 26 and 33 do not establish reachable
independent workflows. This addition does not claim their complete parity.

## Processor allocation and evidence

C5 firmware 60 admits management and data frames during its existing ordinary
passive AP scan. A bounded 32-row table retains exact BSSID, station and actual
receive-channel keys. There is no additional worker, radio job or per-frame
allocation. S3 BLE can continue independently under its own radio owner; S3
also performs validation, presentation, selected vendor lookup and SD export.

Only complete SDK data frames with valid clear address headers and exactly one
DS direction qualify. WDS/ad-hoc, fragments and unsupported framing do not.
QoS/null headers can qualify; protected payloads are never decoded. Counts are
observed callbacks including retries, not unique packets or connected clients.
Locally administered unicast addresses are valid observations, not reliable
vendor identities. Admission closes before Stop/error invalidates the bank;
late callbacks must still match the original full-width epoch and arm time.

Each direction retains its own latest callback time and RSSI validity. STA > AP
signal belongs to the station transmitter; AP > STA signal belongs to the AP.
An invalid latest RSSI is unavailable rather than replaced by an older value.
Overflow is counted explicitly after the first 32 retained keys. Zero matching
retained rows, unavailable capture, unsupported firmware and transport failure
remain separate outcomes. None proves association, complete coverage or absence.

## Selection and response behavior

An ordinary AP publication is bracketed by read-only relation-bank fences. The
bound peer, full 64-bit epoch and bank status accompany a nonreused local AP
publication token. The incremental reader must match that original proof before
and after copying rows. An eight-bit scan-generation match alone is insufficient.
Failed fences preserve valid AP rows but prohibit an unbound client fetch.

The selected network's Observed Clients page uses the committed scan evidence.
Opening, pagination and details do not start reception. Selections freeze the
AP identity and evidence by value; another AP or channel cannot borrow an old
selection's publication. Later scans cannot change already displayed evidence.
Direction-specific age bounds include transfer delay without comparing S3 and
C5 uptime origins or resetting the evidence's age on publication.

S3 transfers at most one physical SPI attempt per UI tick, releasing the mutex
between chunks. Hidden pages, sleep and Back cancel local reads without issuing
an RF Stop. Failed reads and allocation failures do not retry continuously.
An explicit Retry uses the original capture proof; failed capture fences require
a new AP scan. The store is allocated once in PSRAM on demand. Optional C5
telemetry is deferred during the transfer. The two AP capture fences have 100 ms
software budgets; an in-flight native exchange can overrun one deadline. The
pre-existing AP row capture is still synchronous. No hard touch-response or
scan-throughput guarantee has been measured.

The existing focused station capture remains available through Focused Scan
Options. Ordinary relationship evidence never populates its station-selection
or active-action eligibility banks. The legacy path remains available to older
C5 firmware. Active transmission implementations are unchanged.

Save SD formats only numeric and hexadecimal evidence in a temporary bounded
8 KiB PSRAM buffer, then uses the existing Wi-Fi report writer. Its C5 callback
timestamps are uptime microseconds, not UTC. Normal list rows do not perform
SD lookups; only selected details use the shared vendor cache.

## Integrated Wardriving

The same immutable relation bank joins each completed onboard Wi-Fi sweep in
the main Wardriving service. Records retain both direction counts, the exact
latest direction/time and that transmitter's RSSI. They are always unlocated,
even when a GPS fix exists. Existing AP/BLE/expansion records keep their own
location rules. See [CSV v4 semantics](WARDRIVE_WIGLE_FORMAT.md).

A separate bounded dedupe history distinguishes AP and station signal changes.
It commits only after the SD append succeeds. Stop retains the unread suffix
of the immutable staged bank, preventing a restart from replaying its saved
prefix. No extra task, file stream, submenu or GPS requirement is introduced.

## Validation limits

Host checks exercise the actual parser/callback/lifecycle, exact-proof transport,
model, UI controls/rendering, report bounds and Wardriving storage/Stop behavior.
All 79 suites and five native targets passed with stable source inputs. Startup
has an online 40 MHz C5 link, mounted SD, ten S3 tasks and no new reset fault.
The relation store is 4,744 target bytes in PSRAM on demand, including its
200-byte reader, 1,496-byte bank and 1,512-byte selected subset. Report export
temporarily adds 8 KiB. Wardriving's session workspace is 20,864 bytes, a 2,248
byte increase; its observation stays 80 bytes. The C5 table is 1,464 bytes.
Contained types are not separate allocations.

Static internal RAM increases by 128 bytes on S3 and 1,456 on C5. Measured idle
free internal RAM is 116,064 and 64,468 bytes respectively, with 1,428,324 free
S3 PSRAM bytes. These figures do not measure session peak allocation or rate.
The [initial owner check](../../integration/device-test-2026-10-27/rev80-owner-clients-acceptance.json)
confirms that the selected-AP page opens with client rows. Read-only diagnostics
show two retained relationships and four callbacks with no capacity drops in
the whole completed bank. This is not a verified association count. Physical
checks remain for detailed direction/signal/age presentation, vendor/SD use,
concurrent BLE reception and repeated cleanup. A passing synthetic
test is not evidence of radio throughput or touchscreen latency. Wi-Fi NO MEM,
Association Sleep and Bad Message reports remain unresolved; full Marauder
parity, GPS and the six-C5 expansion board remain separate outstanding work.

# Passive MAC Monitor

Revision 78 is installed as a paired S3/C5 update, with C5 firmware 59.
Physical reception, sustained transfer, touchscreen operation and repeated
cleanup acceptance are pending. Overall mode coverage remains partial.

The reference is Marauder develop
[`79cc6bc`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
checked again on 2026-10-30. Its MAC Monitor route selects
`WIFI_SCAN_DETECT_FOLLOW` and combines Wi-Fi and BLE observations in a ranked
table. Cerberus implements that passive workflow in original C code. GPS-based
movement or following detection is not implemented or inferred from signal
strength. No GPS is required for the available monitor.

## Use and interpretation

Open Scan -> MAC MONITOR, then START. Opening the page allocates its display
state but does not start either radio. C5 monitors supported Wi-Fi source
headers while S3 listens for BLE advertising reports. The combined list sorts
by observed count, signal or recent observation. Selecting a painted row freezes
that source identity, so concurrent ranking changes cannot open a different
source. Its detail view retains the selection and displays subsequent matching
observations separately.

Wi-Fi and BLE use separate source namespaces. BLE address type is part of its
key. An address is an observed radio identifier, not proof of a physical device
or person. Randomized addresses can change. Counts describe admitted callback
observations, including Wi-Fi retries and BLE response/partial reports; they
are not a count of unique over-the-air transmissions. BLE response and partial
report flags remain explicit. Latest RSSI and time describe the latest admitted
report, which can be a response or partial report. Unavailable RSSI stays
unavailable instead of becoming a fabricated signal reading.

Each producer holds at most 32 current sources. Deterministic replacement and
invalid-input counters remain visible. Old observations do not become recent
merely because a status poll succeeded. The age display uses a conservative
bound anchored before the first snapshot request; duplicate or retried banks
cannot refresh it. Details retain an earlier source if its producer row has
been replaced. This is a bounded local inventory, not an exhaustive RF census.
Known retained totals and source history survive STOP -> START while the page
remains allocated. They are not per-start counters or complete lifetime totals:
bounded eviction can lose earlier history. Closing the page clears the local
inventory.

STOP and BACK retain state until both radio reservations are released. Back
from a detail returns to the list; Back from the list requests cleanup before
leaving. Hiding the page or sleeping also requests cleanup. No traffic is
generated, and this mode does not write observations to the SD card.

## Processor allocation

C5 reuses the existing scanner worker and passively cycles through legal,
SDK-accepted channels in both supported bands. It does not monitor every
channel simultaneously. Retuning closes callback admission and drains pending
work before changing channels. The source parser validates successful receive
metadata, frame control, supported header lengths, channel and source address.
Management and data headers and fixed transmitter-address-bearing control
headers are supported. Variable BAR/BA control layouts and headers without a
usable transmitter address are excluded.

The callback aggregates into a session-only collector. It performs no SPI or
UI work. A second bank pins an immutable snapshot until acknowledged. The
reader transfers at most four 20-byte chunks per UI service step, verifies CRC
and every row, and rechecks the source boot, epoch, descriptor and clock before
publishing. A lost acknowledgement can retry without adding the same counts
twice. The pinned bank keeps its original request-time anchor after a retry.

S3 reuses its passive BLE scanner and host. An owned monitor session rearms
finite eight-second passive sweeps while retaining its collector between
sweeps. Callback epoch checks reject late reports from an earlier sweep.
Active snapshots are atomic and use the same validated inventory format as
C5. No additional task or per-report heap allocation is introduced.

The S3 controller, two adapters, inventory, staging, selection and render cache
share one on-demand PSRAM allocation. C5 allocates its collector and pinned
snapshot only while needed. Cleanup failures retain the relevant reservation
and memory until restoration succeeds. Boot identity, caller job, full-width
capture epoch and non-reused callback admission tokens fence stale operations.
A renewable C5 lease also bounds unattended ownership after communication loss.

The read-only `radio` console exposes separate `C5_MAC` and `S3_MAC` aggregate
blocks. Reading diagnostics cannot start, renew or pin a capture. Diagnostic
success establishes transport and state reporting; it does not establish RF
reception or screen acceptance.

## Validation status

Focused host fixtures exercise the shared collector, actual producer callbacks,
actual SPI adapter, inventory, session controller, renderer and live UI route.
They cover malformed frames and banks, replacement, count saturation,
clock/reboot boundaries, lost Start/Stop/ACK replies, ownership ambiguity,
incremental transfer, immutable selection and release-before-free behavior.
The actual live UI fixture covers 25 balanced allocation lifetimes.

The [rev78 receipt](../../integration/device-test-2026-10-27/rev78-flash-receipt.json)
binds 72 passing host suites, five native firmware targets with identical source
fingerprints throughout, matched images and nine verified image writes. Early native attempts
identified strict indentation and bounded-label formatting warnings; their
evidence is preserved separately from the successful final run. Rendered list,
detail and maximum-counter screens were inspected, and the renderer fixture
checks incremental pixels against a full redraw.

Frozen ELF layouts measure one 13,600-byte S3 PSRAM live store and one
2,632-byte C5 internal store. These include their collectors, staging and
pinned banks; contained type sizes must not be counted as additional
allocations. Each wire row is 40 bytes and a full snapshot is 1,308 bytes.
The S3 store also reuses the existing scanner/host infrastructure, whose
allocation is separate. Static internal sections grow by 48 bytes on S3 and
280 bytes on C5 relative to revision 77. These are measured memory costs,
not a claim of improved RF throughput. Live peak memory and sustained
performance still require physical measurement.

Settled startup diagnostics show C5 firmware 59 online at 40 MHz, the SD card
mounted, no previous reset fault, and both MAC producers idle with zero counts
and no error. S3 has 116,272 bytes of free internal RAM, 1,428,324 bytes of free
PSRAM and ten tasks. C5 has 65,924 bytes of free internal RAM, 288 fewer than
the revision-77 startup baseline; the net measured change includes its
280-byte static increase. S3's 48-byte free-memory change matches its static
increase. Idle PSRAM and task count are unchanged. No radio session was
started as part of this installation verification.

The existing Wi-Fi NO MEM, Association Sleep and Bad Message reports remain
unresolved. Remote ID Follow remains deferred for the owner's later test.
This checkpoint does not establish complete Marauder feature parity.

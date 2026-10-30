# CSI Motion Monitor

Revision 77 is installed as a paired S3/C5 update, with C5 firmware 58. Physical
CSI reception, format, sensitivity, sustained transfer and repeated cleanup
acceptance remain pending. Overall mode coverage is partial.

The source reference is Marauder develop
[`79cc6bc`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
checked again on 2026-10-30. Its reachable monitor selects one scanned AP,
calibrates, displays motion/reliability/features/history, and supports
recalibration and Stop. Cerberus implements this workflow with original C code
and shared ownership of the existing radio worker. Its scores and thresholds
are heuristic analysis; numerical equivalence and physical sensitivity have
not been established. Upstream's active CLI controls are not ported into the
read-only diagnostic console. Overall mode coverage remains partial.

## Use

Scan Wi-Fi, choose an AP, and open CSI MOTION beside PACKET MONITOR. Opening
the page freezes the AP identity and channel and allocates its S3 state, but
does not start capture. START requests passive capture on the C5. No GPS,
association or generated probe traffic is required. S3 BLE remains independent.

Keep the environment still while calibration collects 30 adequate one-second
windows. Sparse traffic can make calibration take longer than 30 seconds;
silence displays LOW DATA. The page shows score, reliability, sample rate,
four feature scores, counters and 160 seconds of history. Gaps represent
insufficient data. RECALIBRATE releases the old capture before beginning a new
one. STOP and BACK retain ownership while cleanup is pending; Back returns to
the same AP menu only after release. Leaving or sleeping requests cleanup.

Some AP traffic may not provide usable CSI. The correct result is LOW DATA
when too little supported traffic arrives. A motion result describes variation
in the received signal; the feature does not determine a person's identity.

## Processing and format

C5 uses the installed ESP-IDF 6.0.1 CSI API with forced LLTF and explicit 8-bit
imaginary/real pairs. It accepts the documented 106-byte layout, valid estimate
metadata, the selected transmitter and actual channel. Unsupported lengths and
metadata are counted and rejected without truncation. The format and callback
requirements come from the
[Espressif C5 guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/api-guides/wifi-driver/wifi-vendor-features.html#wi-fi-channel-state-information)
and installed SDK declarations, archived in the primary reference index.

The callback copies into a 16-record ring allocated only for a session. It does
no model calculation or SPI work. C5's existing scanner worker handles setup,
lease expiry and restoration. CSI support adds driver memory cost at Wi-Fi
initialization; that cost must be measured separately from the 1,984-byte ring
payload and module static state. No new task is created, and Wi-Fi is not
deinitialized between sessions.

S3 runs a normalized amplitude, shape, detrended phase and temporal-change
model in one PSRAM allocation containing session, view and render cache. It
conservatively omits the first two complex tones for every sample, giving a
stable 51-tone mask despite the first-word-invalid flag. Carrier indexes follow
the declared secondary-channel layout; a layout change resets calibration.
Median/MAD calibration, source-time windows, bounded time jumps and invalid
history markers prevent missing data from becoming a stillness result.

## Ownership and evidence

Every Start names a C5 boot identity and a monotonically increasing caller job.
A private full-width capture epoch fences each admitted session. Callback
tokens never wrap or reuse. Source timestamps are sampled under the admission
lock; a clock watermark includes queued and in-flight callbacks, so S3 cannot
advance empty windows past unpublished older data. These are callback times,
not physical packet arrival timestamps.

The SPI reader checks the boot, job, epoch and frozen target; pins a record;
reads bounded chunks; verifies CRC and canonical metadata; and rechecks the
head and source clock before publishing. ACK retries cannot feed the model
twice. A complete different target proven before epoch adoption releases the
local claim without controlling that capture; malformed proof remains ambiguous.
Fresh status polls cannot hide stalled sample delivery: stale samples display
LOW DATA with unavailable scores, without advancing the source-time history.
A missing Start response remains ambiguous until an authoritative poll
resolves ownership. A five-second lease is renewed during normal operation.
Failed cleanup retains the reservation and buffer until restoration succeeds.

The read-only `radio` console adds a C5_CSI block with state, ownership,
receiving/channel, queue depth, format/capacity and error. Once a session exists,
it reports accepted, invalid, overflow, acknowledged and pending counts. These
are local processing counters, not proof of all RF traffic or motion accuracy.

The existing Wi-Fi NO MEM, Association Sleep and Bad Message reports remain
unresolved. Remote ID Follow remains deferred for the owner's later test.

## Installed validation and memory

The [rev77 receipt](../../integration/device-test-2026-10-27/rev77-flash-receipt.json)
binds 65 passing host suites, five stable-input firmware builds, matched images
and nine verified image writes. Focused checks exercise the model, callback
producer, transport, ownership, renderer and actual UI route. Fault cases include
lost replies, pinned-record retries, reboot/target changes, stale samples and
balanced allocation cleanup. Rendered normal and stale screens were inspected.
The first build compiled all five targets but normalized one SDK configuration
comment; its evidence is preserved separately from the clean final run.

Frozen target layout measures one 4,040-byte S3 PSRAM store and a 1,984-byte C5
ring payload, both on demand. Contained model/view/cache sizes are not additional
allocations. Static internal sections grow 16 bytes on S3 and 152 bytes on C5.
Settled startup has 116,320 bytes free internal S3 RAM, 1,428,324 bytes free PSRAM,
ten S3 tasks and 66,212 bytes free internal C5 RAM. C5 idle free memory is 5,264
bytes below rev76, including static and runtime costs; this is a feature cost,
not a memory saving. Peak capture heap and stack use remain unmeasured.

The 40 MHz C5 link is online, SD is mounted, and the new CSI diagnostic block is
idle with no error, format 1 and capacity 16. These checks establish installation
and idle health, not RF reception, motion accuracy or touchscreen acceptance.

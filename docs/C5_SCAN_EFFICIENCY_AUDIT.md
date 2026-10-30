# C5 scan efficiency audit, 2026-10-29

This is a source audit against the rev51 device observation: 23 recovered
Wardriving C5 status timeouts in 57.4 seconds, with 11 successful Wi-Fi sweeps.
No firmware source was changed by this audit.

## Subsequent implementation, 2026-10-30 UTC

Rev52's final no-GPS session recorded 79 recovered mutex acquisition timeouts
in 253.9 seconds, with 52 C5 Wi-Fi and 31 S3 BLE sweeps. That confirms the
contention persisted; it does not identify how many missed READY pulses
occurred. Rev53 installed the shared scanner/packet-monitor worker described
below. Rev54 installs the following READY correction after all five target
builds, final ELF inspection, matched packaging and nine write hashes pass.
The [rev54 receipt](../../integration/device-test-2026-10-27/rev54-flash-receipt.json)
records startup and live evidence.

A 22.592-second no-GPS run finishes STOPPED with four C5 Wi-Fi sweeps, two
S3 BLE sweeps, 72 unlocated rows, one saved file, no service errors and zero
scan-status timeouts. BLE host ownership returns to zero. At 1,397 exchanges,
transport counters remain at four initial READY timeouts/fallbacks seen at
startup, with zero mutex/fall/high timeouts or driver errors. The maximum
mutex wait is 25 microseconds. Hold/exchange maxima still include startup;
there is no per-session reset of those maxima. This short acceptance run is
not duration-matched to rev52 and does not establish a long-run speedup.

The S3 now latches READY falling edges in an IRAM GPIO handler. Each request
and response transfer snapshots a separate epoch before SPI transmission,
waits for that epoch to change, then requires READY high. The wait does not
accept the low level alone: an ISR pending at that moment could otherwise
be consumed by the following transfer. A whole low/high pulse before the
first poll is retained. Deadlines use elapsed monotonic microseconds,
including time spent preempted; unsigned subtraction handles clock wrap.
Frame CRC, command, sequence, payload length, three-attempt retry policy and
the existing no-READY legacy fallback are unchanged. The wire packet format
and C5 firmware do not change.

Local ESP-IDF 6.0.1 `esp_driver_gpio/src/gpio.c` and `driver/gpio.h` were
reviewed for service behavior. A newly installed service uses
`ESP_INTR_FLAG_IRAM`; an existing per-pin service returns
`ESP_ERR_INVALID_STATE` and is shared, never uninstalled. The handler is
IRAM-safe in either case. If an existing service is non-IRAM, its delivery
can still be delayed while flash cache is disabled. Handler installation
failures explicitly fail link initialization and disable that pin's
interrupt. The handler uses only lock-free 32-bit atomic loads/stores to a
DRAM counter, with compile-time size/alignment assertions; it has one writer.
The driver serializes per-pin callbacks on the ISR service's assigned core.

The first firmware build rejected a generic `ATOMIC_INT_LOCK_FREE == 2`
assertion. IDF enables `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND`, adding
`-mdisable-hardware-atomics` and reporting that generic RMW capability as 1.
It does not make these aligned loads/stores call library support. Compiling
the actual production translation unit with the generated S3 command and
those exact flags confirms the ISR emits only `entry`, `l32r`, `memw`,
`l32i.n`, `addi.n`, `memw`, `s32i.n`, and `retw.n`. The counter is four bytes
and four-byte aligned in DRAM; the ISR and its literal are in IRAM. The
generic lock-free assertion remains for other targets/host checks.
`tools/check_c5_ready_iram.py` now gates the S3 build on the actual linked
ELF: it rejects calls, RMW instructions or other unreviewed instructions,
checks internal-memory placement, and verifies the sole IRAM literal points
to the DRAM counter. Atomic fetch-add/CAS must not replace the single-writer
load/store pair without a new target-level review.

The read-only `c5` console command now reports local transport counters even
if the latest status request fails: exchanges, mutex acquisition timeouts,
initial-high/falling-epoch/requeue-high timeouts, legacy fallback, driver
errors, and maximum mutex wait, onboard mutex hold, and exchange duration.
`spi_ready_latched_pulses` counts cycles whose falling epoch is already
latched while READY is high when inspected. It is evidence of retained
short cycles, not a measured count of failures in an older image. All
counter snapshots require no additional SPI transaction. Expansion hold
duration is not included in onboard hold statistics; its contention can
affect the wait statistic.

Source correction after rev54: the three read-only helpers in
`c5_watch_link.h` (Watch status, chunked Watch result, and C5 BLE activity)
still acquired/released `link_mutex` directly in the installed rev54 image.
Their SPI exchanges were counted, but their mutex acquisition failures and
wait/hold durations bypassed the new wrappers. The source now uses the same
`take_link_mutex`/`give_link_mutex` wrappers as the rest of `c5_spi_link.c`.
This correction is installed in rev55 after host checks and all five builds.
The short no-GPS Wardriving retest passes with no new SPI timeouts or driver
errors. It does not revise the already collected rev54 counters into complete
Watch timing; separate Watch-mode live timing acceptance remains open.

The production-header host checks exercise all three failed acquisitions
(250 ms counted, no SPI transfers or payload publication), successful wait
versus hold timing, unsupported capabilities, malformed status/chunks/records,
CRC retry exhaustion, a changed before/after snapshot, invalid BLE activity,
and invalid indices. Every acquired path releases exactly once. Framing,
generation/identity checks, immutable pending copies and response validation
are unchanged. Counters still describe onboard link calls, not every user of
the shared physical bus; expansion traffic does not acquire through these
wrappers, although its contention can increase an onboard caller's wait.

`tools/check_c5_ready_handshake.py` compiles the production transport and
framing functions with deterministic hardware substitutes. It covers a
complete pulse during SPI transfer, delayed low/high, no ISR despite low,
stuck high/low, legacy low, the separate response epoch, CRC/command/sequence/
length rejection, existing retry limits, mutex acquisition failure without
a transmitted command, distinct wait/hold duration, initialization failures,
shared service and idempotent initialization, and clock/epoch wraparound.
Event-pump cadence and Wardriving scheduling are deliberately unchanged for
an interpretable hardware comparison. Actual interrupt timing, heap cost,
Wardriving throughput and Stop latency still need device acceptance.

## Proven timeout source

`s3_main/main/c5_spi_link.c::wifi_scan_status_command()` returns
`ESP_ERR_TIMEOUT` only when `link_mutex` cannot be acquired within 100 ms.
`exchange_v2_locked()` returns `ESP_ERR_INVALID_RESPONSE` after failed wire
responses. The counted Wardriving timeouts therefore establish **S3 shared SPI
bus lock contention**, not a C5 scan-response timeout. The blocking
`esp_wifi_scan_start()` call in `wifi_scanner.c::wifi_scan_task()` runs outside
`scanner_mutex`; the C5 status getter only copies a bounded struct under it.

Other confirmed bus users remain active during Wardriving: the event pump
requests C5 status and queue status every 500 ms, then drains event batches;
the UI requests status every five seconds. The Wardriving idle expansion
probe gate removes R1–R6 idle probes, but does not pause these users.

There is also a concrete handshake race to measure:
`spi_request_response_sized()` waits for READY low *after* each SPI transfer.
The C5 lowers READY while preparing the response and raises it as soon as
the next transaction is queued. There is no enforced minimum low time. A
low pulse can finish before the S3 starts its level poll or between its
20-us polls. A missed pulse spends 100 ms waiting even when the eventual
response is valid; this happens while the shared bus mutex is held. Source
shows the race, but rev51 diagnostics do not prove how frequently it occurred.

Recommended bus patch sequence:

1. Add transport counters for mutex acquisition timeouts, READY-low/high
   timeouts, fallback use, and maximum transaction/lock hold duration. Expose
   them through the existing read-only console. This distinguishes long
   handshake waits from event traffic without guessing from one aggregate.
2. Latch READY falling edges with a GPIO ISR counter. Snapshot its epoch
   before each transaction, wait for an epoch change (including an edge
   that happened during the transfer), then wait for READY high. Preserve
   the legacy path only when a READY cycle truly times out. Validate ISR
   memory/order behavior on S3 rather than replacing the wait with a fixed
   delay.
3. Once measured, reduce redundant event-pump device-status reads by caching
   capabilities and refreshing device uptime on a slower heartbeat or queue
   failure. Keep queue draining/backpressure responsive. Merely raising the
   scan mutex wait from 100 to 250 ms can hide the symptom; it does not remove
   bus occupancy.

Host tests should replay a READY low/high pulse before the first poll, a
delayed response, a stuck-high line, a stuck-low line, and legacy no-READY
behavior. Assert no next command consumes a prior response, bounded waits,
and the same sequence/CRC validation. On-device acceptance compares recovered
timeouts, lock hold counters, completed sweeps, Stop latency and S3 BLE sweeps
over an equal-duration Wardriving run.

## Proven packet monitor memory cost

`c5_firmware/main/wifi_scanner.c` creates `packet_monitor_task` at initialization
with a 4,096-byte stack, even while idle. Its dedicated ring has 64 packed
12-byte samples (768 bytes), plus small counters/status and the task control
block. This accounts for the observed approximately 5 KiB idle heap/static
memory increase relative to the prior image. The ring is useful retained
state; the extra stack is avoidable.

The smallest safe memory patch is to reuse the existing 4,096-byte
`wifi_scan_task`, which already dispatches exclusive AP scan, station and
capture work. Refactor the packet task's one-job body into
`packet_monitor_run()`, dispatch it when `packet_monitor_radio_reserved` is
set, notify `scanner_task_handle` on monitor Start, and remove the dedicated
task handle/creation. The monitor already excludes AP scan, capture,
connections and defense; it preempts channel survey before reservation.
Keep the reservation until promiscuous RX is disabled and the prior band and
channel are restored. Keep existing failed-restore Stop retries.

Prefer this shared worker over creating/deleting a monitor task at every
Start: it avoids a dynamic task lifetime race and saves the same stack at
idle and during other modes. Tests must cover monitor Start, Stop before
worker execution, stale notifications, failed disable/restore with retained
reservation, retry Stop, then successful AP scan/capture start. Measure C5
idle heap recovery and confirm Packet Monitor activity/exit on hardware.

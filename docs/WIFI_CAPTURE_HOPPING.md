# Passive beacon capture radio contract

Current installation is S3 rev58 with the byte-identical C5 50 images from
paired rev56. This C5 capture contract is unchanged. S3 now shares one
mode-scoped analysis session manager and lazy PSRAM bank between MultiSSID
and PineScan. Their routes are Scan -> Wi-Fi -> TOOLS -> MULTISSID/PINESCAN ->
START. Classifier, lifecycle, UI/navigation host checks, five stable builds,
matched-package validation and three S3 write hashes pass; rev58 touch/RF
acceptance remains pending.

Paired rev56 was installed with onboard C5 firmware 50. All five stable build
targets, matched-package validation and nine flash-write hash checks passed.
The S3 session and UI are integrated. The device owner reports increasing
MultiSSID beacon counts, successful Stop and a subsequent normal Wi-Fi scan.
This is an owner report of broad reception and that cleanup transition. A
read-only console snapshot on the same boot showed capture state 0/generation
0 with no retained capture; normal Wi-Fi scans increased from 0 to 2 with zero
failures and the device was idle. That snapshot corroborates normal scanning
and idle state, not the reported MultiSSID run. Capture-specific validation is
unresolved pending live counters from a repeat left running. Controlled
three-SSID group detection, live selected Follow and saved PCAP contents remain
unverified. The host checks below exercise the
production C5 service and private scanner lifecycle with simulated driver
boundaries. Neither those checks nor the owner report establish both-band or
per-channel RF coverage, receive throughput, or full S3/UI acceptance.

## Compatible request and transport

The existing capture commands, packed payload sizes, complete-frame CRC32,
20-byte chunking and generation/sequence ACK protocol are unchanged.

- `MODE_BEACON = 32` selects beacon frames. The C5 checks subtype, minimum
  fixed-header length, version and fragmentation/protection flags. It sends
  the complete frame to the S3 for strict IE/SSID parsing; this family filter
  does not itself claim a well-formed beacon body.
- `FLAG_HOP = 2` requires channel zero, beacon-only mode and no BSSID filter
  flag. No AP scan, selected AP, SSID or BSSID is needed. The S3 must gate this
  contract and beacon mode on firmware 50 or newer.
- Beacon mode also permits an ordinary fixed-channel request. MultiSSID
  signal follow must match the selected transmitter (`addr2`) on the S3;
  a BSSID (`addr3`) filter is a different scope.
- Existing fixed-channel modes retain their radio behavior, filter scope,
  lease, request identity and payload framing.
  Their Stop/setup rollback now also waits for admitted callbacks before
  releasing ownership; a failed drain/disable remains retryable.
- Status channel reports current RX admission, or zero while hopping is
  paused/stopped. Each pinned record retains its actual RX-control channel,
  RSSI and C5 `esp_timer_get_time()` value taken by the promiscuous callback.
  This is a boot-monotonic callback timestamp, not UTC, a hardware RX timestamp
  or the later SPI fetch time. A beacon's advertised DS channel cannot change
  this receiver provenance.

## Radio lifecycle

The existing scanner worker services the capture job; there is no extra task
or retained task stack. One session/ring generation and record-sequence stream
spans every hop. Queued frames remain pinned until ACK, including across hops,
Stop, and failed radio cleanup. A later generation cannot start until the ring
is drained.

The candidate plan is channels 1–14 followed by 36, 40, 44, 48, 52, 56, 60,
64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153,
157, 161 and 165. A successful channel dwells for at least 250 ms, subject to
the existing 50 ms worker service cadence and scheduling. Driver-rejected
channels are skipped; a complete unsuccessful pass stops with the first
driver error. No country setting or SDK restriction is overridden. This plan
does not promise every candidate is available in every regulatory domain.

Before retuning, admission is set to channel zero, promiscuous reception is
disabled, and admitted callbacks get up to 50 ms to drain. Only then can band
and channel change. Admission opens on the selected channel after a successful
enable. No SSID parsing, allocation, file operation or driver operation occurs
in the receive callback. The bounded two-record ring and its explicit full
drop counter remain unchanged; sustained traffic may exceed its drain rate.

Hopping remembers the previous band mode, primary channel and secondary
channel. Stop first closes admission and the ring, then disables RX, drains
callbacks, restores the previous channel/band mode and releases the radio.
Any failed disable/drain/restore retains the reservation. The worker retries
cleanup, and explicit Stop can retry too. Setup/hop errors remain visible;
an explicit successful same-generation Stop acknowledges `STOPPED`, retaining
the original service error where available. Stop is idempotent after release.

The finite transport lease remains 1,000–300,000 ms. Only valid peek/read/ACK
transport activity renews it. Channel hops and status polls do not renew it.
Expiry closes the session and restores the radio even if the UI worker has
disappeared. Continuous analysis therefore needs an actively draining S3
consumer, not an unbounded C5 lease.

The current admission review relies on the actual C5 configuration: one CPU,
promiscuous callbacks in the Wi-Fi task (priority 23), and control tasks at
priorities 5 and 4. The compiled atomic check/increment gap has no call or
yield, so those lower-priority controls cannot run inside it on this target.
See Espressif's [callback context](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/api-guides/wifi-driver/wifi-modes.html)
and [task priorities](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/api-guides/performance/speed.html).
This does not establish a general multicore/deferred-callback guarantee, or
that disabling promiscuous mode drains all queued callbacks. A future port
that changes those scheduling assumptions needs generation-bound admission
at callback entry. Ring retention and explicit admitted-callback draining
remain in place; no payload buffer is freed from a receive callback.

## Host verification

Run these from the unified project with the installed Python/ziglang toolchain:

```text
py -3.14 tools/check_wifi_capture_filter.py
py -3.14 tools/check_wifi_capture_service.py
py -3.14 tools/check_wifi_capture_radio.py
```

The radio harness includes the production `wifi_capture_radio_service.h` and
`wifi_capture_service.c`. It covers both-band progression, SDK channel rejects,
complete-cycle failure, a queued record surviving hops with the same CRC and
sequence, source channel/time preservation, callback injection at every tune
boundary, delayed callback drain and timeout, setup/enable/disable/restore
faults, retained reservations, manual and worker cleanup retries, Stop ACK
after rollback, wrap-safe lease expiry, idempotent start/stop, pending-ring
generation exclusion, and existing fixed-channel plus fixed beacon operation.

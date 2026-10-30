# 802.11 capture and export contract

`shared/wifi_pcap.h` and `.c` provide a standalone, allocation-free PCAPNG
serializer. They do not enable radio capture, SPI transfer or SD writing.
`tools/check_wifi_pcap.py` checks exact bytes and malformed inputs on the host.

## Pinned source and Cerberus gap

The pinned Marauder checkout is `511bb3694d0c4dc064888e762bde44fcc9b7f48a`.
`esp32_marauder/WiFiScan.cpp` has separate probe, deauthentication, EAPOL and raw
capture modes. `Buffer.cpp` writes raw 802.11 PCAP records using boot-relative
`micros()`. `PcapHeader.cpp` sets linktype 105 (raw IEEE 802.11). Those files do
not preserve the receive channel and RSSI in their PCAP records. Cerberus's
coverage ledger still marks these receive-capture modes pending. The existing
AP-job reception counters and directed probe transmitter are narrower tools.

The current SPI frame carries 22 payload bytes (`shared/cerberus_protocol.h`),
and its generic fragment service transfers an object of at most 256 bytes in
16-byte data pieces. That service handles a diagnostic snapshot, not sustained
raw frames. The existing S3 Wi-Fi report limit is 256 KiB and uses whole-report
storage (`s3_main/main/storage_report_limits.h`, `storage.c`); a capture session
needs a separate append-only writer and size/rotation policy. S3 SD access has
storage ownership and mutex rules, including USB mass-storage ownership.

## File format and time

The codec emits [PCAPNG](https://datatracker.ietf.org/doc/draft-ietf-opsawg-pcapng/)
with one Section Header Block and one Interface Description Block at file
creation. The IDB uses LINKTYPE_IEEE802_11_RADIOTAP (127), snaplen 4112,
`if_name` of `C5`, `S3` or `R1` through `R6`, and explicit microsecond
`if_tsresol`. **Use one file per physical receiver.** A Simple Packet Block
implicitly belongs to the first interface, so combining radios in one file
would mislabel one of them.

Each record begins with a 16-byte [radiotap](https://radiotap.org/) header:
Flags, [Channel](https://radiotap.org/fields/Channel) frequency and band flag,
and optional signed [Antenna signal](https://radiotap.org/fields/Antenna%20signal.html)
in dBm. The channel flag is `0x0080` for 2 GHz and `0x0100` for 5 GHz.
The [Flags](https://radiotap.org/fields/Flags.html) `0x10` bit is set only when
the captured frame actually includes an FCS. The caller must establish that
from driver metadata; the codec does not infer it from frame bytes. RSSI is
omitted from radiotap when unknown. The supported channel range is 1–14 or
32–177; the codec does not claim a regulatory channel policy or 6 GHz support.

For **verified Unix microseconds**, a record is an Enhanced Packet Block with
an explicit UTC timestamp. A plausible date by itself does not verify the
clock. For **monotonic-only time**, a record is a Simple Packet Block with no
timestamp. The [PCAPNG specification](https://datatracker.ietf.org/doc/draft-ietf-opsawg-pcapng/)
defines the enhanced packet timestamp against UTC, while the simple block has
none. A boot-relative count in the enhanced packet field would appear as a
false wall-clock time. The capture owner must save the monotonic microseconds,
radio boot/session ID and record sequence in a sidecar index if relative timing
is needed. GPS absence does not prevent capture; it means no GPS-derived UTC.
Until C5 and S3 clock offset and error are measured, do not convert one radio's
monotonic count using the other radio's wall clock.

`original_length` is the raw 802.11 length, excluding the synthetic radiotap
prefix. The codec enforces a 4096-byte maximum captured frame and a 65,535-byte
maximum reported original frame. It emits at most 4144 bytes per record. It
rejects malformed channels, invalid length relationships, a known positive
RSSI, an FCS claim shorter than four bytes, an implausible UTC numeric value,
invalid time kind or insufficient output capacity without modifying the output.
Simple Packet Blocks cannot encode an arbitrary captured length; truncation is
accepted there only when the 4096-byte frame cap is reached, so a reader can
recover the captured length from snaplen. Otherwise the caller must drop the
record and count it. Enhanced Packet Blocks encode both lengths explicitly.

## Live integration sequence

1. Reserve the C5 Wi-Fi owner and select a capture mode. Apply AP/BSSID and
   optional client filters from the same immutable target snapshot used by
   the menu. Probe, deauth, EAPOL and raw capture need separate receive
   classifiers and status, with a common bounded frame path. Verify the C5
   SDK's frame-length and FCS semantics before setting codec metadata.
2. On capture start, allocate a small fixed C5 frame queue under a measured
   internal-heap floor; two 4096-byte slots are an initial upper bound, not a
   measured safe allocation. Copy metadata and bytes in the promiscuous
   callback, then return. Never call SPI, the codec, filesystem or UI there.
   Increment separate oversize, queue-full, invalid-metadata and filter-drop
   counters. Expose high-water mark and C5 heap/stack minima.
3. Add a dedicated S3-polled capture transfer protocol. The existing 256-byte
   fragment object is insufficient. A record descriptor should include job
   generation, radio, sequence, captured/original lengths, channel, RSSI
   validity/value, FCS validity, time kind/value and CRC. Chunk reads should
   carry explicit offsets and a bounded data length within the 22-byte SPI
   payload. S3 acknowledges only after complete CRC-checked assembly; a
   repeated descriptor/chunk must be safe after a link retry. A cancelled job
   or lease loss must invalidate old generations. Report transport gaps.
4. On S3, validate a complete record, call `cerberus_wifi_pcapng_record`, and
   append its returned bytes to the appropriate receiver's PCAPNG file. An S3
   local 2.4 GHz receiver can feed the same writer directly under S3 Wi-Fi
   ownership. S3 remains the control, interpretation and storage partner for
   5 GHz C5 capture; its own RF cannot observe 5 GHz. Do not promise concurrent
   S3 web AP and receiver on different channels.
5. Add an S3 append writer with the existing SD mutex/USB ownership rules.
   Write the 68-byte file header once, serialize complete records, rotate by
   explicit size limit, flush on stop/error and report media/write failures.
   Keep callback queues distinct from SD buffers. A monotonic sidecar should
   be written with matching sequence/drop counters. Exercise link loss, slow
   SD, stop during transfer and USB ownership changes on the attached unit.

This codec has host-byte tests only. No production caller, SPI command, SD
writer or live packet-capture path is connected yet; receive-mode parity and
dual-radio throughput remain to be verified on hardware.

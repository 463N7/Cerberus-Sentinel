#!/usr/bin/env python3
"""Offline Cerberus SPI protocol layout and CRC test vectors."""

import struct
import zlib


MAGIC = 0x43455242
TRANSFER_SIZE = 32
FRAME_PAYLOAD_SIZE = 22
EVENT_BATCH_TRANSFER_SIZE = 64
PROTOCOL_V2 = 2
STATUS_FORMAT = "<BBBBIIIHHBB"
WIFI_SCAN_STATUS_FORMAT = "<BBHBB2xi"
WIFI_SCAN_RESULT_REQUEST_FORMAT = "<BB"
WIFI_SCAN_RESULT_FORMAT = "<BBbBBB6s10x"
WIFI_SCAN_SSID_REQUEST_FORMAT = "<BBB"
WIFI_SCAN_SSID_FORMAT = "<BBBB18s"
WIFI_RADIO_STATS_FORMAT = "<BBHBBBBBBBbBBIHH"
WIFI_DEFENSE_STATUS_FORMAT = "<8BIiHH"
WIFI_DEFENSE_RESULT_REQUEST_FORMAT = "<BB"
WIFI_DEFENSE_RESULT_FORMAT = "<BBBBHHHHHHbbHH"
BLE_SCAN_STATUS_FORMAT = "<BBHBB2xi"
BLE_SCAN_STATUS_RAW_FORMAT = "<BBHBBHi"
BLE_SCAN_RESULT_REQUEST_FORMAT = "<BB"
BLE_SCAN_RESULT_FORMAT = "<BBbBBB6sHB7x"
BLE_SCAN_RESULT_RAW_FORMAT = "<BBbBBB6sHBBBBH2x"
BLE_SCAN_DATA_REQUEST_FORMAT = "<BBBB"
BLE_SCAN_DATA_FORMAT = "<BBBBB17s"
BLE_CONNECT_REQUEST_FORMAT = "<B6s"
BLE_CONNECTION_STATUS_FORMAT = "<BBB6sHi7x"
BLE_GATT_SERVICE_STATUS_FORMAT = "<BBHBB2xi"
BLE_GATT_SERVICE_RESULT_REQUEST_FORMAT = "<BB"
BLE_GATT_SERVICE_RESULT_FORMAT = "<B16sHHB"
BLE_GATT_CHARACTERISTIC_DISCOVER_FORMAT = "<HH"
BLE_GATT_CHARACTERISTIC_STATUS_FORMAT = "<BBHBBHHi"
BLE_GATT_CHARACTERISTIC_RESULT_REQUEST_FORMAT = "<BB"
BLE_GATT_CHARACTERISTIC_RESULT_FORMAT = "<B16sHHB"
BLE_GATT_READ_REQUEST_FORMAT = "<H"
BLE_GATT_READ_STATUS_FORMAT = "<BBBBHHHi8s"
BLE_GATT_READ_DATA_REQUEST_FORMAT = "<BB"
BLE_GATT_READ_DATA_FORMAT = "<BBB19s"
IEEE802154_ENERGY_STATUS_FORMAT = "<BBBBi4x"
IEEE802154_ENERGY_RESULT_REQUEST_FORMAT = "<B"
IEEE802154_ENERGY_RESULT_FORMAT = "<BBB16b3x"
IEEE802154_BEACON_STATUS_FORMAT = "<BBHBBBBi"
IEEE802154_BEACON_RESULT_REQUEST_FORMAT = "<BB"
IEEE802154_BEACON_RESULT_FORMAT = "<BBBbBBH8sHBBBB"
IEEE802154_ACTIVITY_STATUS_FORMAT = "<BBBBIi"
IEEE802154_ACTIVITY_RESULT_REQUEST_FORMAT = "<BB"
IEEE802154_ACTIVITY_RESULT_FORMAT = "<BB20B"
C5_EVENT_QUEUE_STATUS_FORMAT = "<BBHIIHBBBB"
C5_EVENT_FLOW_CONTROL_FORMAT = "<BBBB"
C5_EVENT_FORMAT = "<HBBBBIiII"
C5_EVENT_BATCH_HEADER_FORMAT = "<IBBBBBHHBB3x"
FRAGMENT_INFO_FORMAT = "<BBHIBB12x"
FRAGMENT_READ_REQUEST_FORMAT = "<BBH"
FRAGMENT_READ_FORMAT = "<BBHBB16s"
C5_DIAGNOSTIC_SNAPSHOT_FORMAT = "<7I2H19B13x"


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def make_frame(command: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > FRAME_PAYLOAD_SIZE:
        raise ValueError("payload is too large")
    body = struct.pack("<IBBBB", MAGIC, PROTOCOL_V2, command, sequence, len(payload))
    body += payload.ljust(FRAME_PAYLOAD_SIZE, b"\0")
    return body + struct.pack("<H", crc16_ccitt_false(body))


def frame_is_valid(frame: bytes) -> bool:
    if len(frame) != TRANSFER_SIZE:
        return False
    magic, version, _command, _sequence, payload_length = struct.unpack("<IBBBB", frame[:8])
    stored_crc = struct.unpack("<H", frame[-2:])[0]
    return (
        magic == MAGIC
        and version == PROTOCOL_V2
        and payload_length <= FRAME_PAYLOAD_SIZE
        and stored_crc == crc16_ccitt_false(frame[:-2])
    )


def main() -> None:
    assert crc16_ccitt_false(b"123456789") == 0x29B1
    assert zlib.crc32(b"123456789") == 0xCBF43926

    frame = make_frame(command=1, sequence=0x5A)
    assert len(frame) == TRANSFER_SIZE
    assert frame_is_valid(frame)

    status = struct.pack(STATUS_FORMAT, 1, 1, 3, 6, 21, 5000, 0x3FF, 121, 116, 0x05, 0)
    assert frame_is_valid(make_frame(command=1, sequence=0x5B, payload=status))

    corrupted = bytearray(frame)
    corrupted[12] ^= 0x01
    assert not frame_is_valid(bytes(corrupted))

    invalid_length = bytearray(frame)
    invalid_length[7] = FRAME_PAYLOAD_SIZE + 1
    invalid_length[-2:] = struct.pack("<H", crc16_ccitt_false(invalid_length[:-2]))
    assert not frame_is_valid(bytes(invalid_length))

    assert struct.calcsize(STATUS_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(WIFI_SCAN_STATUS_FORMAT) == 12
    assert struct.calcsize(WIFI_SCAN_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(WIFI_SCAN_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(WIFI_SCAN_SSID_REQUEST_FORMAT) == 3
    assert struct.calcsize(WIFI_SCAN_SSID_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(WIFI_RADIO_STATS_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(WIFI_DEFENSE_STATUS_FORMAT) == 20
    assert struct.calcsize(WIFI_DEFENSE_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(WIFI_DEFENSE_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_SCAN_STATUS_FORMAT) == 12
    assert struct.calcsize(BLE_SCAN_STATUS_RAW_FORMAT) == 12
    assert struct.calcsize(BLE_SCAN_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(BLE_SCAN_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_SCAN_RESULT_RAW_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_SCAN_DATA_REQUEST_FORMAT) == 4
    assert struct.calcsize(BLE_SCAN_DATA_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_CONNECT_REQUEST_FORMAT) == 7
    assert struct.calcsize(BLE_CONNECTION_STATUS_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_GATT_SERVICE_STATUS_FORMAT) == 12
    assert struct.calcsize(BLE_GATT_SERVICE_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(BLE_GATT_SERVICE_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_GATT_CHARACTERISTIC_DISCOVER_FORMAT) == 4
    assert struct.calcsize(BLE_GATT_CHARACTERISTIC_STATUS_FORMAT) == 14
    assert struct.calcsize(BLE_GATT_CHARACTERISTIC_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(BLE_GATT_CHARACTERISTIC_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_GATT_READ_REQUEST_FORMAT) == 2
    assert struct.calcsize(BLE_GATT_READ_STATUS_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(BLE_GATT_READ_DATA_REQUEST_FORMAT) == 2
    assert struct.calcsize(BLE_GATT_READ_DATA_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(IEEE802154_ENERGY_STATUS_FORMAT) == 12
    assert struct.calcsize(IEEE802154_ENERGY_RESULT_REQUEST_FORMAT) == 1
    assert struct.calcsize(IEEE802154_ENERGY_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(IEEE802154_BEACON_STATUS_FORMAT) == 12
    assert struct.calcsize(IEEE802154_BEACON_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(IEEE802154_BEACON_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(IEEE802154_ACTIVITY_STATUS_FORMAT) == 12
    assert struct.calcsize(IEEE802154_ACTIVITY_RESULT_REQUEST_FORMAT) == 2
    assert struct.calcsize(IEEE802154_ACTIVITY_RESULT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(C5_EVENT_QUEUE_STATUS_FORMAT) == 18
    assert struct.calcsize(C5_EVENT_FLOW_CONTROL_FORMAT) == 4
    assert struct.calcsize(C5_EVENT_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(C5_EVENT_BATCH_HEADER_FORMAT) == 18
    assert struct.calcsize(FRAGMENT_INFO_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(FRAGMENT_READ_REQUEST_FORMAT) == 4
    assert struct.calcsize(FRAGMENT_READ_FORMAT) == FRAME_PAYLOAD_SIZE
    assert struct.calcsize(C5_DIAGNOSTIC_SNAPSHOT_FORMAT) == 64

    scan_status = struct.pack(WIFI_SCAN_STATUS_FORMAT, 2, 7, 41, 32, 3, 0)
    assert frame_is_valid(make_frame(command=4, sequence=0x20, payload=scan_status))

    result = struct.pack(
        WIFI_SCAN_RESULT_FORMAT,
        7,
        0,
        -54,
        149,
        3,
        32,
        bytes.fromhex("001122334455"),
    )
    assert frame_is_valid(make_frame(command=5, sequence=0x21, payload=result))

    ssid = struct.pack(WIFI_SCAN_SSID_FORMAT, 7, 0, 0, 18, b"Cerberus Test Netw")
    assert frame_is_valid(make_frame(command=6, sequence=0x22, payload=ssid))

    radio_stats = struct.pack(
        WIFI_RADIO_STATS_FORMAT,
        7,
        32,
        41,
        18,
        14,
        3,
        29,
        3,
        9,
        11,
        -41,
        149,
        1,
        5178,
        4,
        1,
    )
    assert frame_is_valid(make_frame(command=50, sequence=0x22, payload=radio_stats))

    defense_status = struct.pack(
        WIFI_DEFENSE_STATUS_FORMAT, 2, 4, 3, 3, 3, 0, 1, 0, 3012, 0, 1000, 0
    )
    assert frame_is_valid(make_frame(command=53, sequence=0x23, payload=defense_status))
    defense_result = struct.pack(
        WIFI_DEFENSE_RESULT_FORMAT,
        4, 0, 6, 1, 210, 175, 420, 32, 2, 1, -91, -38, 1000, 32,
    )
    assert frame_is_valid(make_frame(command=54, sequence=0x24, payload=defense_result))

    ble_status = struct.pack(BLE_SCAN_STATUS_FORMAT, 2, 9, 17, 17, 4, 0)
    assert frame_is_valid(make_frame(command=9, sequence=0x23, payload=ble_status))

    ble_result = struct.pack(
        BLE_SCAN_RESULT_FORMAT,
        9,
        0,
        -61,
        1,
        8,
        2,
        bytes.fromhex("FFEEDDCCBBAA"),
        0x004C,
        1,
    )
    assert frame_is_valid(make_frame(command=10, sequence=0x24, payload=ble_result))

    ble_data = struct.pack(BLE_SCAN_DATA_FORMAT, 9, 0, 1, 0, 8, b"Cerberus".ljust(17, b"\0"))
    assert frame_is_valid(make_frame(command=11, sequence=0x25, payload=ble_data))

    # Revision 30 reuses reserved bytes and a new DATA kind, retaining legacy fields.
    raw_status = struct.pack(BLE_SCAN_STATUS_RAW_FORMAT, 2, 9, 17, 17, 4, 65535, 0)
    assert struct.unpack(BLE_SCAN_STATUS_FORMAT, raw_status) == struct.unpack(BLE_SCAN_STATUS_FORMAT, ble_status)
    raw_result = struct.pack(BLE_SCAN_RESULT_RAW_FORMAT, 9, 0, -61, 1, 8, 2,
                             bytes.fromhex("FFEEDDCCBBAA"), 0x004C, 1, 31, 3, 0, 65535)
    assert struct.unpack(BLE_SCAN_RESULT_FORMAT, raw_result) == struct.unpack(BLE_SCAN_RESULT_FORMAT, ble_result)
    assert frame_is_valid(make_frame(command=10, sequence=0x26, payload=raw_result))
    raw_data = bytes(range(31))
    for chunk, offset in enumerate(range(0, 31, 17)):
        part = raw_data[offset:offset + 17]
        payload = struct.pack(BLE_SCAN_DATA_FORMAT, 9, 0, 3, chunk, len(part), part.ljust(17, b"\0"))
        assert frame_is_valid(make_frame(command=11, sequence=0x27 + chunk, payload=payload))

    ble_address = bytes.fromhex("FFEEDDCCBBAA")
    ble_connect = struct.pack(BLE_CONNECT_REQUEST_FORMAT, 1, ble_address)
    assert frame_is_valid(make_frame(command=12, sequence=0x26, payload=ble_connect))

    ble_connection_status = struct.pack(
        BLE_CONNECTION_STATUS_FORMAT,
        2,
        3,
        1,
        ble_address,
        1,
        0,
    )
    assert frame_is_valid(make_frame(command=14, sequence=0x27, payload=ble_connection_status))

    ble_service_status = struct.pack(BLE_GATT_SERVICE_STATUS_FORMAT, 2, 4, 6, 6, 3, 0)
    assert frame_is_valid(make_frame(command=16, sequence=0x28, payload=ble_service_status))

    ble_service_result = struct.pack(
        BLE_GATT_SERVICE_RESULT_FORMAT,
        2,
        bytes.fromhex("0F18").ljust(16, b"\0"),
        0x0020,
        0x0027,
        0,
    )
    assert frame_is_valid(make_frame(command=17, sequence=0x29, payload=ble_service_result))

    ble_characteristic_discovery = struct.pack(
        BLE_GATT_CHARACTERISTIC_DISCOVER_FORMAT,
        0x0020,
        0x0027,
    )
    assert frame_is_valid(make_frame(command=18, sequence=0x2A, payload=ble_characteristic_discovery))

    ble_characteristic_status = struct.pack(
        BLE_GATT_CHARACTERISTIC_STATUS_FORMAT,
        2,
        5,
        3,
        3,
        3,
        0x0020,
        0x0027,
        0,
    )
    assert frame_is_valid(make_frame(command=19, sequence=0x2B, payload=ble_characteristic_status))

    ble_characteristic_result = struct.pack(
        BLE_GATT_CHARACTERISTIC_RESULT_FORMAT,
        2,
        bytes.fromhex("192A").ljust(16, b"\0"),
        0x0021,
        0x0022,
        0x12,
    )
    assert frame_is_valid(make_frame(command=20, sequence=0x2C, payload=ble_characteristic_result))

    ble_read_request = struct.pack(BLE_GATT_READ_REQUEST_FORMAT, 0x0022)
    assert frame_is_valid(make_frame(command=21, sequence=0x2D, payload=ble_read_request))

    ble_read_status = struct.pack(
        BLE_GATT_READ_STATUS_FORMAT,
        2,
        6,
        3,
        0,
        0x0022,
        4,
        4,
        0,
        bytes(8),
    )
    assert frame_is_valid(make_frame(command=22, sequence=0x2E, payload=ble_read_status))

    ble_read_data = struct.pack(
        BLE_GATT_READ_DATA_FORMAT,
        6,
        0,
        4,
        bytes.fromhex("64000000").ljust(19, b"\0"),
    )
    assert frame_is_valid(make_frame(command=23, sequence=0x2F, payload=ble_read_data))

    ieee802154_status = struct.pack(IEEE802154_ENERGY_STATUS_FORMAT, 1, 7, 5, 16, 0)
    assert frame_is_valid(make_frame(command=25, sequence=0x30, payload=ieee802154_status))

    ieee802154_result = struct.pack(
        IEEE802154_ENERGY_RESULT_FORMAT,
        7,
        11,
        16,
        *[-91, -90, -87, -83, -86, -92, -94, -88, -81, -85, -89, -93, -95, -84, -90, -92],
    )
    assert frame_is_valid(make_frame(command=26, sequence=0x31, payload=ieee802154_result))

    ieee802154_beacon_status = struct.pack(
        IEEE802154_BEACON_STATUS_FORMAT,
        2,
        8,
        29,
        3,
        16,
        0,
        0,
        0,
    )
    assert frame_is_valid(make_frame(command=28, sequence=0x32, payload=ieee802154_beacon_status))

    ieee802154_beacon_result = struct.pack(
        IEEE802154_BEACON_RESULT_FORMAT,
        8,
        0,
        15,
        -61,
        214,
        0x07,
        0x4A3C,
        bytes.fromhex("7766554433221100"),
        0x0000,
        2,
        2,
        9,
        0,
    )
    assert frame_is_valid(make_frame(command=29, sequence=0x33, payload=ieee802154_beacon_result))

    ieee802154_activity_status = struct.pack(
        IEEE802154_ACTIVITY_STATUS_FORMAT,
        2,
        9,
        16,
        0,
        37,
        0,
    )
    assert frame_is_valid(make_frame(command=31, sequence=0x34, payload=ieee802154_activity_status))

    ieee802154_activity_result = struct.pack(
        IEEE802154_ACTIVITY_RESULT_FORMAT,
        9,
        11,
        *[1, 8, 3, 2, 0, 0, 5, 1, 1, 0, 2, 4, 2, 1, 0, 0, 7, 0, 0, 0],
    )
    assert frame_is_valid(make_frame(command=32, sequence=0x35, payload=ieee802154_activity_result))

    event_queue_status = struct.pack(
        C5_EVENT_QUEUE_STATUS_FORMAT,
        3,
        16,
        0,
        7,
        4,
        2,
        4,
        12,
        0x03,
        1,
    )
    assert frame_is_valid(make_frame(command=33, sequence=0x36, payload=event_queue_status))

    c5_event = struct.pack(C5_EVENT_FORMAT, 7, 2, 2, 11, 0, 25000, 0, 14, 0)
    assert frame_is_valid(make_frame(command=34, sequence=0x37, payload=c5_event))

    event_flow_control = struct.pack(C5_EVENT_FLOW_CONTROL_FORMAT, 4, 12, 0x01, 0)
    assert frame_is_valid(make_frame(command=35, sequence=0x38, payload=event_flow_control))

    event_batch_request = struct.pack("<B", 2)
    assert frame_is_valid(make_frame(command=36, sequence=0x39, payload=event_batch_request))
    second_c5_event = struct.pack(C5_EVENT_FORMAT, 8, 3, 2, 12, 0, 25010, 0, 9, 0)
    event_batch_body = struct.pack(
        C5_EVENT_BATCH_HEADER_FORMAT,
        MAGIC,
        PROTOCOL_V2,
        36,
        0x39,
        2,
        0,
        0,
        0,
        0x01,
        1,
    ) + c5_event + second_c5_event
    event_batch = event_batch_body + struct.pack("<H", crc16_ccitt_false(event_batch_body))
    assert len(event_batch) == EVENT_BATCH_TRANSFER_SIZE
    assert crc16_ccitt_false(event_batch[:-2]) == struct.unpack("<H", event_batch[-2:])[0]
    corrupted_batch = bytearray(event_batch)
    corrupted_batch[31] ^= 0x01
    assert crc16_ccitt_false(corrupted_batch[:-2]) != struct.unpack("<H", corrupted_batch[-2:])[0]

    diagnostic_snapshot = struct.pack(
        C5_DIAGNOSTIC_SNAPSHOT_FORMAT,
        21,
        30000,
        0x3FF,
        103000,
        98000,
        9,
        9,
        0,
        0,
        *([0] * 19),
    )
    snapshot_crc32 = zlib.crc32(diagnostic_snapshot)
    assert frame_is_valid(make_frame(command=37, sequence=0x3A, payload=struct.pack("<B", 1)))
    fragment_info = struct.pack(
        FRAGMENT_INFO_FORMAT,
        1,
        4,
        len(diagnostic_snapshot),
        snapshot_crc32,
        16,
        0,
    )
    assert frame_is_valid(make_frame(command=37, sequence=0x3A, payload=fragment_info))
    rebuilt_snapshot = bytearray()
    for fragment_index, offset in enumerate(range(0, len(diagnostic_snapshot), 16)):
        read_request = struct.pack(FRAGMENT_READ_REQUEST_FORMAT, 1, 4, offset)
        assert frame_is_valid(make_frame(command=38, sequence=0x3B + fragment_index, payload=read_request))
        chunk = diagnostic_snapshot[offset : offset + 16]
        flags = 1 if offset + len(chunk) == len(diagnostic_snapshot) else 0
        read_payload = struct.pack(
            FRAGMENT_READ_FORMAT,
            1,
            4,
            offset,
            len(chunk),
            flags,
            chunk.ljust(16, b"\0"),
        )
        assert frame_is_valid(
            make_frame(command=38, sequence=0x3B + fragment_index, payload=read_payload)
        )
        rebuilt_snapshot.extend(chunk)
    assert bytes(rebuilt_snapshot) == diagnostic_snapshot
    assert zlib.crc32(rebuilt_snapshot) == snapshot_crc32

    v1_request = struct.pack("<IBBB", MAGIC, 1, 1, 0x5A).ljust(TRANSFER_SIZE, b"\0")
    v1_response = struct.pack("<IBBBBII", MAGIC, 1, 1, 0x5A, 1, 2, 1000).ljust(TRANSFER_SIZE, b"\0")
    assert len(v1_request) == TRANSFER_SIZE
    assert len(v1_response) == TRANSFER_SIZE

    print("Cerberus protocol tests passed")


if __name__ == "__main__":
    main()

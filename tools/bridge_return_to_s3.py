"""Apply the documented 1200-baud bridge handoff and follow the same carrier."""
import argparse
import time
import serial
from serial.tools.list_ports import comports
p = argparse.ArgumentParser()
p.add_argument("--port", required=True)
p.add_argument("--mac", required=True)
args = p.parse_args()
ports = [item for item in comports() if item.device == args.port]
if len(ports) != 1 or (ports[0].vid, ports[0].pid) != (0x303A, 0x4001) or \
        (ports[0].serial_number or "").lower() != args.mac.lower():
    raise RuntimeError("Selected endpoint is not the identified Cerberus bridge")
location = ports[0].location.split(":")[0]
port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
port.dtr = False; port.rts = False; port.port = args.port
try:
    port.open(); port.baudrate = 1200; time.sleep(0.05)
finally:
    port.close()
deadline = time.monotonic() + 25
while time.monotonic() < deadline:
    found = [item for item in comports() if (item.vid, item.pid) == (0x303A, 0x1001) and
        (item.serial_number or "").lower() == args.mac.lower() and
        (item.location or "").split(":")[0] == location]
    if len(found) == 1:
        print(f"Matched S3 ROM endpoint returned on {found[0].device}"); break
    time.sleep(0.25)
else:
    raise RuntimeError("S3 ROM did not return; physical S3 BOOT + RESET is needed")

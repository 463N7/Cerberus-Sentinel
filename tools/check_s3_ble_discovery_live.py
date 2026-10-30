"""Exercise actual ordinary BLE live UI, controller, history and frozen taps."""
from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, str(Path(__file__).with_name("check_s3_ble_discovery_ui.py")),
                "--native-live"], check=True)

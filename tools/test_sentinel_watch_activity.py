"""Exercise the actual passive environmental activity detector."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="sentinel-activity-") as directory:
        executable = Path(directory) / ("activity.exe" if os.name == "nt" else "activity")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-O1", "-g", "-I", str(ROOT / "s3_main/main"),
            str(ROOT / "s3_main/main/sentinel_watch_activity.c"),
            str(ROOT / "tools/tests/sentinel_watch_activity_test.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()

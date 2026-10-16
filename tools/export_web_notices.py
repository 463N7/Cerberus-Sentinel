#!/usr/bin/env python3
"""Collect upstream runtime/firmware license texts for the public installer."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-root", type=Path, help="ESP-IDF used by this firmware build")
    args = parser.parse_args()
    description = json.loads((ROOT / ".pio/build/s3-main/project_description.json").read_text())
    idf = args.idf_root or Path(description["idf_path"])
    node = ROOT / "web/node_modules"
    lock = json.loads((ROOT / "web/package-lock.json").read_text())["packages"]
    sections = [
        "CERBERUS SENTINEL — THIRD-PARTY SOFTWARE NOTICES\n\n"
        "The following notices apply to their named components, not to the Cerberus\n"
        "project as a whole. Third-party projects do not endorse Cerberus Sentinel.\n"
        "Watch catalog notices and protocol-source attributions are also distributed\n"
        "inside each firmware release directory.\n"
    ]

    def append(title: str, source: str, text: str) -> None:
        sections.append(f"\n{'=' * 76}\n{title}\nSource: {source}\n{'=' * 76}\n\n{text.strip()}\n")

    def npm(name: str, filename: str, extra: str = "") -> None:
        package = json.loads((node / name / "package.json").read_text(encoding="utf-8"))
        if package["version"] != lock[f"node_modules/{name}"]["version"]:
            raise ValueError(f"Installed {name} does not match package-lock.json")
        text = (node / name / filename).read_text(encoding="utf-8")
        append(f"{name} {package['version']}", f"npm:{name}@{package['version']} / {filename}", extra + text)

    npm("esptool-js", "LICENSE", "Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.\n\n")
    npm("atob-lite", "LICENSE.md", "Author: Hugh Kennedy.\n\n")
    npm("pako", "LICENSE")
    zlib = (node / "pako/lib/zlib/inflate.js").read_text(encoding="utf-8").splitlines()
    zlib_notice = "\n".join(line[3:] if line.startswith("// ") else "" for line in zlib if line.startswith("//"))
    # This source begins with the upstream zlib notice; stop before implementation comments.
    zlib_notice = zlib_notice.split("3. This notice may not be removed or altered from any source distribution.")[0] + "3. This notice may not be removed or altered from any source distribution."
    append("pako zlib-derived implementation", "npm:pako / lib/zlib/inflate.js", zlib_notice)

    remote = (
        ("SparkMD5 3.0.2 — alternative MIT license", "https://raw.githubusercontent.com/satazor/js-spark-md5/v3.0.2/LICENSE2"),
        ("ESP Flasher Stub v1.2.2 — MIT license", "https://raw.githubusercontent.com/espressif/esptool-js/v0.7.0/src/targets/stub_flasher/LICENSE-MIT"),
    )
    spark = json.loads((node / "spark-md5/package.json").read_text(encoding="utf-8"))
    if spark["version"] != "3.0.2" or spark["version"] != lock["node_modules/spark-md5"]["version"]:
        raise ValueError("Update the pinned SparkMD5 attribution for this dependency version")
    for title, source in remote:
        with urllib.request.urlopen(source, timeout=20) as response:
            text = response.read().decode("utf-8")
        if "Permission is hereby granted" not in text or "Copyright" not in text:
            raise ValueError(f"Unexpected license response: {source}")
        append(title, source, text)
    append("Flasher stub provenance", "https://github.com/espressif/esptool-js/blob/v0.7.0/src/targets/stub_flasher/README.md",
           "esptool-js 0.7.0 contains ESP Flasher Stub v1.2.2 (set 2).\n"
           "Source: https://github.com/espressif/esp-flasher-stub/releases/tag/v1.2.2\n"
           "The upstream stub JSON files are dual licensed Apache-2.0 OR MIT;\n"
           "the MIT alternative is reproduced above. Cerberus uses the upstream\n"
           "library and adjusts transfer limits/reset handling in its own adapter.")

    append(f"ESP-IDF {description['git_revision']}", "https://github.com/espressif/esp-idf", (idf / "LICENSE").read_text())
    sdk_files = (
        "freertos/FreeRTOS-Kernel/LICENSE.md", "lwip/lwip/COPYING",
        "mbedtls/mbedtls/LICENSE", "mbedtls/mbedtls/tf-psa-crypto/LICENSE",
        "esp_libc/COPYING.NEWLIB", "esp_libc/COPYING.picolibc",
        "bt/host/nimble/nimble/LICENSE", "bt/host/nimble/nimble/NOTICE",
        "bt/host/nimble/nimble/ext/tinycrypt/LICENSE", "bt/common/tinycrypt/LICENSE",
        "bt/controller/lib_esp32c5/esp32c5-bt-lib/NOTICE",
        "esp_wifi/lib/LICENSE", "esp_phy/lib/LICENSE", "esp_coex/lib/LICENSE",
        "http_parser/LICENSE.txt", "wpa_supplicant/COPYING", "wpa_supplicant/README",
        "console/linenoise/LICENSE", "console/argtable3/LICENSE",
        "spiffs/spiffs/LICENSE", "protobuf-c/protobuf-c/LICENSE",
    )
    for name in sdk_files:
        append(f"ESP-IDF component: {name}", f"ESP-IDF {description['git_revision']} / components/{name}",
               (idf / "components" / name).read_text(encoding="utf-8"))
    fatfs = (idf / "components/fatfs/src/ff.c").read_text(encoding="utf-8").split("*/", 1)[0] + "*/"
    append("FatFs R0.15 w/patch2", "ESP-IDF / components/fatfs/src/ff.c", fatfs)
    for name, version in (("esp_tinyusb", "2.2.1"), ("tinyusb", "0.21.0~2"), ("qrcode", "0.2.0")):
        append(f"espressif/{name} {version}", f"https://components.espressif.com/components/espressif/{name}",
               (ROOT / "managed_components" / f"espressif__{name}" / "LICENSE").read_text())
    append("espressif/mdns 1.11.3", "https://components.espressif.com/components/espressif/mdns",
           (ROOT / "c5_firmware/managed_components/espressif__mdns/LICENSE").read_text())
    qrcode = (ROOT / "managed_components/espressif__qrcode/qrcodegen.c").read_text().split("*/", 1)[0] + "*/"
    append("Project Nayuki QR Code generator", "https://www.nayuki.io/page/qr-code-generator-library", qrcode)
    append("Watch broadcast signature catalog", "Cerberus Sentinel / docs/WATCH_THIRD_PARTY_NOTICES.md",
           (ROOT / "docs/WATCH_THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8"))
    output = ROOT / "web/public/notices.txt"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(sections), encoding="utf-8", newline="\n")
    print(f"Collected {len(sections) - 1} upstream notice sections: {output}")


if __name__ == "__main__":
    main()

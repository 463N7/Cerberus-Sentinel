"""Use the documented 4 KiB C5 bridge blocks without patching installed tools."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path.home() / ".platformio/packages/tool-esptoolpy"))
import esptool
from esptool.targets.esp32c5 import ESP32C5ROM, ESP32C5StubLoader
ESP32C5ROM.ESP_RAM_BLOCK = 0x1000
ESP32C5StubLoader.FLASH_WRITE_SIZE = 0x1000
esptool._main()

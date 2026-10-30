# Local ESP32-C5 build support

Vendored from the installed official PlatformIO `espressif32@7.0.1` package.
Apache-2.0 license is retained in LICENSE. Runtime builder, board metadata,
monitor and platform files are included; examples and CI are omitted.

The upstream platform's ESP-IDF configuration recognizes C5 through IDF, but
its SCons compiler selection still treats C5 as Xtensa. A clean baseline failed
with `xtensa-esp32c5-elf-gcc` not found.

Local change: add `esp32c5` to existing `esp32c3, esp32c6` MCU lists in:

- `platform.py`: toolchain package selection and boot/debug classification.
- `builder/main.py`: RISC-V compiler, binutils and debugger prefix.
- `builder/frameworks/espidf.py`: compiler package, RISC-V linker fragments,
  ULP exclusion and zero bootloader offset.

C5 project configurations use a relative `symlink://` path to this platform.
The global PlatformIO installation is not patched. IDF remains pinned to 6.0.1.
This is build support only; flashing/debugging and physical operation still
require validation on the actual board.

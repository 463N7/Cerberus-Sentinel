# Verified C5 build toolchain

The C5 builds use ESP-IDF 6.0.1 and its recommended RISC-V GCC release `esp-15.2.0_20251204`. On this Windows workstation, a fresh PlatformIO registry install of `platformio/toolchain-riscv32-esp@15.2.0+20251204` lacked `g++`, `ar`, `as`, and `gcc-ar`, so CMake could not configure a clean C5 build. Do not mix programs from another compiler version into that package.

The shared `c5_firmware/platformio.ini` and `expansion/c5_scanner/platformio.ini` pin `platformio/toolchain-riscv32-esp@15.2.0+20251204`. Each optionally loads `platformio.local.ini` from its own project directory. That filename is ignored by Git: Linux and CI checkouts use the registry pin without a Windows path or environment-variable dependency.

On this Windows workstation, the local overrides select an independent, verified copy in `%USERPROFILE%\.platformio\tools\verified-riscv32-esp-15.2\riscv32-esp-elf`. It came from the [Espressif release](https://github.com/espressif/crosstool-NG/releases/tag/esp-15.2.0_20251204), archive `riscv32-esp-elf-15.2.0_20251204-x86_64-w64-mingw32.zip`. Its SHA-256 is `c61488aa15f49146aae918267110f775a52c3cef3844cbf261f475ef97523c3d`, matching `framework-espidf/tools/tools.json` in the installed ESP-IDF. The original archive is retained under `%USERPROFILE%\.platformio\tools\verified-downloads`.

The archive's `riscv32-esp-elf/package.json` was adapted only as a local PlatformIO package manifest: `name` is `toolchain-riscv32-esp` and `version` is `15.2.0+20251204`. The original manifest is retained as `package.espressif-original.json` alongside it. Before building on another Windows machine, install and hash-check that same official archive, extract it at the expected path, adapt the manifest, and confirm `gcc.exe`, `g++.exe`, `gcc-ar.exe`, `ar.exe`, `as.exe`, `ld.exe`, and `objcopy.exe` exist in `bin`.

Create `platformio.local.ini` in both C5 project directories with the actual absolute extraction path, using forward slashes:

```ini
[env]
platform_packages =
    platformio/framework-espidf@4.60001.0
    toolchain-riscv32-esp@symlink://C:/Users/YOUR_USER/.platformio/tools/verified-riscv32-esp-15.2/riscv32-esp-elf
```

Keep both entries: [PlatformIO extra configurations](https://docs.platformio.org/en/latest/projectconf/sections/platformio/options/generic/extra_configs.html) replace the entire `platform_packages` value. Check the resolved values with `python -m platformio project config -d c5_firmware --json-output` and the same command with `-d expansion/c5_scanner`. The normal `pio run`, Sentinel build wrapper, and integration build wrapper all read these overrides automatically. Do not copy workstation overrides into CI.

After switching compiler packages, run `pio run -t clean` for the onboard, expansion WROOM, and expansion XIAO environments to remove old CMake compiler paths. A successful target build must show the verified package path in `project_description.json` (`c_compiler`) and the diagnostic tool entries in `CMakeCache.txt`. `tools/build_toolchain.py` resolves those actual build-selected tools and rejects missing tools, stale metadata, or tools from a different compiler directory; diagnostics do not fall back to a guessed registry path.

Matched Sentinel receipts fingerprint present local overrides as source inputs and record each target's compiler version, compiler/diagnostic-tool hashes, and toolchain/framework manifest identities. Packaging rechecks that provenance. These focused identities do not hash every library in the compiler installation; the original archive check remains the installation integrity check. Changing an override or selected compiler invalidates the current matched receipt and requires a new matched build. The archived firmware candidate and flash receipt, not a successful compiler run alone, determine what is installed on hardware.

param(
    [string]$Port = "",
    [int]$C5Baud = 115200,
    [string]$C5Env = "c5-onboard",
    [string]$C5PartitionFile = "partitions_2mb.csv",
    [switch]$SkipC5Build,
    [switch]$ValidateOnly,
    [switch]$LeaveBridge
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$C5Project = Join-Path $Root "c5_firmware"
$Pio = Join-Path $env:APPDATA "Python\Python314\Scripts\pio.exe"
$Esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$C5Target = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool\targets\esp32c5.py"
$C5Build = Join-Path $C5Project ".pio\build\$C5Env"
$FlashArgs = Join-Path $C5Build "flash_args"
$C5PartitionCsv = Join-Path $C5Project $C5PartitionFile
$C5PartitionBin = Join-Path $C5Build "partitions.bin"
$C5FlashSize = 0x200000

if (!(Test-Path $Pio)) {
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "PlatformIO was not found. Install PlatformIO or update `$Pio in this script."
    }
    $Pio = $cmd.Source
}

if (!(Test-Path $Esptool)) {
    throw "esptool.py was not found at $Esptool"
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Block
    )

    Write-Host ""
    Write-Host "== $Name ==" -ForegroundColor Cyan
    & $Block
}

function Ensure-C5SmallBlocks {
    if (!(Test-Path $C5Target)) {
        throw "ESP32-C5 esptool target file was not found at $C5Target"
    }

    $source = Get-Content $C5Target -Raw
    if ($source -match "FLASH_WRITE_SIZE\s*=\s*0x1000") {
        Write-Host "C5 esptool flash block size is already 4 KB."
        return
    }

    if ($source -notmatch "FLASH_WRITE_SIZE\s*=\s*0x4000") {
        throw "Could not find the expected ESP32-C5 FLASH_WRITE_SIZE setting to patch."
    }

    $backup = "$C5Target.cerberus-backup"
    if (!(Test-Path $backup)) {
        Copy-Item $C5Target $backup
    }

    $patched = $source -replace "FLASH_WRITE_SIZE\s*=\s*0x4000", "FLASH_WRITE_SIZE = 0x1000"
    Set-Content -Path $C5Target -Value $patched -NoNewline
    Write-Host "Patched C5 esptool flash block size to 4 KB."
}

function Get-C5FlashPairs {
    if (!(Test-Path $FlashArgs)) {
        throw "C5 flash_args was not found. Build the C5 firmware first."
    }

    $pairs = @()
    foreach ($line in Get-Content $FlashArgs) {
        $trimmed = $line.Trim()
        if ($trimmed -notmatch "^0x[0-9a-fA-F]+\s+\S+") {
            continue
        }

        $parts = $trimmed -split "\s+"
        $address = $parts[0]
        $relativeImage = $parts[1]
        $imagePath = Join-Path $C5Build $relativeImage

        if (!(Test-Path $imagePath)) {
            $fallback = Join-Path $C5Build (Split-Path $relativeImage -Leaf)
            if (Test-Path $fallback) {
                $imagePath = $fallback
            }
        }

        if (!(Test-Path $imagePath)) {
            if ($relativeImage -like "*bootloader*") {
                $fallback = Join-Path $C5Build "bootloader.bin"
            } elseif ($relativeImage -like "*partition*") {
                $fallback = Join-Path $C5Build "partitions.bin"
            } else {
                $fallback = Join-Path $C5Build "firmware.bin"
            }

            if (Test-Path $fallback) {
                $imagePath = $fallback
            }
        }

        if (!(Test-Path $imagePath)) {
            throw "C5 image listed in flash_args was not found: $relativeImage"
        }

        $pairs += $address
        $pairs += $imagePath
    }

    if ($pairs.Count -eq 0) {
        throw "No C5 flash images were found in $FlashArgs"
    }

    return $pairs
}

function ConvertTo-C5ByteSize {
    param([string]$Value)

    $trimmed = $Value.Trim()
    if ($trimmed -match "^0x[0-9a-fA-F]+$") {
        return [Convert]::ToInt64($trimmed.Substring(2), 16)
    }
    if ($trimmed -match "^(\d+)([KkMm])?$") {
        $size = [int64]$Matches[1]
        if ($Matches[2] -match "[Kk]") {
            return $size * 1024
        }
        if ($Matches[2] -match "[Mm]") {
            return $size * 1024 * 1024
        }
        return $size
    }
    throw "Invalid partition size or offset: $Value"
}

function Get-C5FactoryPartition {
    if (!(Test-Path $C5PartitionCsv)) {
        throw "C5 partition table was not found: $C5PartitionCsv"
    }

    $partitions = @()
    foreach ($line in Get-Content $C5PartitionCsv) {
        $content = ($line -split "#", 2)[0].Trim()
        if ($content.Length -eq 0) {
            continue
        }
        $fields = $content -split ","
        if ($fields.Count -lt 5) {
            throw "Invalid C5 partition row: $line"
        }
        $partitions += [PSCustomObject]@{
            Name = $fields[0].Trim()
            Type = $fields[1].Trim()
            SubType = $fields[2].Trim()
            Offset = ConvertTo-C5ByteSize $fields[3]
            Size = ConvertTo-C5ByteSize $fields[4]
        }
    }

    $previousEnd = 0x9000
    foreach ($partition in $partitions | Sort-Object Offset) {
        if (($partition.Offset % 0x1000) -ne 0 -or ($partition.Size % 0x1000) -ne 0) {
            throw "C5 partition $($partition.Name) is not aligned to 4 KB."
        }
        if ($partition.Offset -lt $previousEnd) {
            throw "C5 partition $($partition.Name) overlaps a preceding flash region."
        }
        $partitionEnd = $partition.Offset + $partition.Size
        if ($partitionEnd -gt $C5FlashSize) {
            throw "C5 partition $($partition.Name) exceeds the 2 MB flash boundary."
        }
        $previousEnd = $partitionEnd
    }

    $factory = @($partitions | Where-Object { $_.Type -eq "app" -and $_.SubType -eq "factory" })
    if ($factory.Count -ne 1) {
        throw "C5 partition table must contain exactly one factory application slot."
    }
    return $factory[0]
}

function Confirm-C5ImagesFit {
    param(
        [object]$FactoryPartition,
        [object[]]$FlashPairs
    )

    if (!(Test-Path $C5PartitionBin)) {
        throw "Generated C5 partition image was not found. Build the C5 firmware first."
    }
    if ((Get-Item $C5PartitionBin).LastWriteTimeUtc -lt (Get-Item $C5PartitionCsv).LastWriteTimeUtc) {
        throw "Generated C5 partition image is older than partitions_2mb.csv. Rebuild without -SkipC5Build."
    }

    $appImage = $null
    for ($index = 0; $index -lt $FlashPairs.Count; $index += 2) {
        $address = ConvertTo-C5ByteSize ([string]$FlashPairs[$index])
        if ($address -eq $FactoryPartition.Offset) {
            $appImage = [string]$FlashPairs[$index + 1]
            break
        }
    }
    if ($null -eq $appImage) {
        throw "No C5 application image is assigned to the factory partition offset."
    }

    $appBytes = (Get-Item $appImage).Length
    if ($appBytes -gt $FactoryPartition.Size) {
        throw "C5 application image ($appBytes bytes) exceeds its factory slot ($($FactoryPartition.Size) bytes)."
    }
    Write-Host "C5 factory slot: 0x$($FactoryPartition.Offset.ToString('X')) + 0x$($FactoryPartition.Size.ToString('X'))"
    Write-Host "C5 application: $appBytes / $($FactoryPartition.Size) bytes"
}

function Invoke-Tool {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArgs)

    if ($CommandArgs.Count -lt 1) {
        throw "Invoke-Tool was called without a program."
    }

    $program = $CommandArgs[0]
    if ($CommandArgs.Count -gt 1) {
        $programArgs = $CommandArgs[1..($CommandArgs.Count - 1)]
    } else {
        $programArgs = @()
    }

    & $program @programArgs
    if ($LASTEXITCODE -ne 0) {
        throw "$program failed with exit code $LASTEXITCODE"
    }
}

function Get-SentinelUsbPorts {
    $json = & python -c 'import json; from serial.tools.list_ports import comports; print(json.dumps([dict(device=p.device, vid=p.vid, pid=p.pid, serial=p.serial_number, location=p.location) for p in comports()]))'
    if ($LASTEXITCODE -ne 0) { throw "Could not enumerate USB ports." }
    return @($json | ConvertFrom-Json)
}

function Find-SentinelPort {
    param([int]$UsbPid, [string]$UsbSerial = "")
    $found = @(Get-SentinelUsbPorts | Where-Object {
        $_.vid -eq 0x303A -and $_.pid -eq $UsbPid -and
        ![string]::IsNullOrWhiteSpace($_.location) -and
        ($_.location -split ':')[0] -eq $SentinelUsbLocation -and
        ([string]::IsNullOrWhiteSpace($UsbSerial) -or $_.serial -eq $UsbSerial)
    })
    if ($found.Count -gt 1) { throw "More than one matching Cerberus USB endpoint was found." }
    if ($found.Count -eq 1) { return $found[0] }
    return $null
}

function Wait-SentinelPort {
    param([int]$UsbPid, [string]$UsbSerial = "")
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $found = Find-SentinelPort -UsbPid $UsbPid -UsbSerial $UsbSerial
        if ($null -ne $found) { return $found }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Cerberus did not reappear on its original USB connection. Use BOOT + RESET for S3 ROM recovery."
}

if (!$SkipC5Build) {
    Invoke-Step "Build C5 base firmware" {
        Push-Location $C5Project
        try {
            Invoke-Tool -CommandArgs @($Pio, "run", "-e", $C5Env)
        } finally {
            Pop-Location
        }
    }
}

$c5Images = Get-C5FlashPairs
$c5Factory = Get-C5FactoryPartition
Confirm-C5ImagesFit -FactoryPartition $c5Factory -FlashPairs $c5Images

if ($ValidateOnly) {
    Write-Host ""
    Write-Host "C5 images and partition layout are valid. No device was modified." -ForegroundColor Green
    return
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "Choose the connected Cerberus port explicitly with -Port COMx. Validation is available with -ValidateOnly."
}

$identity = @(Get-SentinelUsbPorts | Where-Object { $_.device -eq $Port })
if ($identity.Count -ne 1 -or $identity[0].vid -ne 0x303A -or
    $identity[0].pid -ne 0x1001 -or [string]::IsNullOrWhiteSpace($identity[0].serial) -or
    [string]::IsNullOrWhiteSpace($identity[0].location)) {
    throw "The selected port must be the Cerberus S3 native USB Serial/JTAG endpoint."
}
$SentinelUsbSerial = $identity[0].serial
$SentinelUsbLocation = ($identity[0].location -split ':')[0]
$BridgePort = $null
$flashFailure = $null

Invoke-Step "Prepare local C5 flasher for the S3 bridge" {
    Ensure-C5SmallBlocks
}

try {
    Invoke-Step "Load S3-to-C5 flash bridge" {
        Push-Location $Root
        try {
            Invoke-Tool -CommandArgs @($Pio, "run", "-e", "s3-c5-bridge", "-t", "upload", "--upload-port", $Port)
        } finally {
            Pop-Location
        }
    }

    $bridgeIdentity = Wait-SentinelPort -UsbPid 0x4001
    $BridgePort = $bridgeIdentity.device
    Write-Host "C5 bridge enumerated on $BridgePort at the same USB connection."

    Invoke-Step "Confirm onboard C5 identity" {
        Invoke-Tool -CommandArgs @("python", $Esptool, "--chip", "esp32c5",
            "--port", $BridgePort, "--baud", "$C5Baud", "--before", "no_reset",
            "--after", "no_reset", "flash_id")
    }

    Invoke-Step "Clear old C5 boot header at 0x0" {
        Invoke-Tool -CommandArgs @(
            "python", $Esptool,
            "--chip", "esp32c5",
            "--port", $BridgePort,
            "--baud", "$C5Baud",
            "--before", "no_reset",
            "--after", "no_reset",
            "erase_region", "0x0", "0x1000"
        )
    }

    Invoke-Step "Erase C5 app slot" {
        $appOffset = "0x$($c5Factory.Offset.ToString('X'))"
        $appSize = "0x$($c5Factory.Size.ToString('X'))"
        Invoke-Tool -CommandArgs @(
            "python", $Esptool,
            "--chip", "esp32c5",
            "--port", $BridgePort,
            "--baud", "$C5Baud",
            "--before", "no_reset",
            "--after", "no_reset",
            "erase_region", $appOffset, $appSize
        )
    }

    Invoke-Step "Flash C5 base firmware through S3 bridge" {
        $args = @(
            "python", $Esptool,
            "--chip", "esp32c5",
            "--port", $BridgePort,
            "--baud", "$C5Baud",
            "--before", "no_reset",
            "--after", "no_reset",
            "write_flash",
            "--verify",
            "--no-compress",
            "--flash_size", "keep",
            "--flash_mode", "keep",
            "--flash_freq", "keep"
        ) + $c5Images

        Invoke-Tool -CommandArgs $args
    }
} catch {
    $flashFailure = $_
} finally {
    if (!$LeaveBridge) {
        try {
            Invoke-Step "Restore normal S3 touchscreen firmware" {
                $bridgeIdentity = Find-SentinelPort -UsbPid 0x4001
                if ($null -ne $bridgeIdentity) {
                    Invoke-Tool -CommandArgs @("python", "-c",
                        "import serial,sys,time; s=serial.Serial(sys.argv[1],115200,timeout=0.1); s.baudrate=1200; time.sleep(0.05); s.close()",
                        $bridgeIdentity.device)
                }
                $s3Identity = Wait-SentinelPort -UsbPid 0x1001 -UsbSerial $SentinelUsbSerial
                Push-Location $Root
                try {
                    Invoke-Tool -CommandArgs @($Pio, "run", "-e", "s3-main", "-t", "upload", "--upload-port", $s3Identity.device)
                    # USB RTS reset can retain a download boot latch. The
                    # esptool S3 watchdog reset was verified to boot the app.
                    # This reads a volatile register; it writes no flash/eFuse.
                    Invoke-Tool -CommandArgs @("python", $Esptool,
                        "--chip", "esp32s3", "--port", $s3Identity.device,
                        "--before", "default_reset", "--after", "watchdog_reset",
                        "read_mem", "0x6000812C")
                } finally {
                    Pop-Location
                }
            }
        } catch {
            if ($null -ne $flashFailure) {
                Write-Warning "S3 restore also failed: $($_.Exception.Message)"
            } else {
                $flashFailure = $_
            }
        }
    } else {
        Write-Host ""
        Write-Host "Leaving S3 in C5 bridge mode." -ForegroundColor Yellow
    }
}

if ($null -ne $flashFailure) { throw $flashFailure }

Write-Host ""
Write-Host "C5 flash flow complete." -ForegroundColor Green

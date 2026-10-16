import { ESPLoader, Transport } from 'esptool-js';
import SparkMD5 from 'spark-md5';
import { sha256, validateImage } from './release.js';

const USB_VENDOR = 0x303a;
const NATIVE_PID = 0x1001;
const BRIDGE_PID = 0x4001;
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const profiles = {
  bridge: {chip: 'esp32s3', name: 'ESP32-S3', id: 9, capacity: 0x1000000},
  s3: {chip: 'esp32s3', name: 'ESP32-S3', id: 9, capacity: 0x1000000},
  c5: {chip: 'esp32c5', name: 'ESP32-C5', id: 23, capacity: 0x200000},
};

export function md5Bytes(bytes) {
  // The input can be a view into a larger buffer; hash only the selected bytes.
  return new SparkMD5.ArrayBuffer().append(bytes.buffer.slice(
    bytes.byteOffset, bytes.byteOffset + bytes.byteLength,
  )).end();
}

function assertPort(port, product) {
  const info = port?.getInfo?.();
  if (info?.usbVendorId !== USB_VENDOR || info?.usbProductId !== product) {
    throw new Error(product === BRIDGE_PID
      ? 'Select the Cerberus C5 bridge USB connection.'
      : 'Select the Cerberus S3 USB connection.');
  }
}

function canonicalMac(value) {
  if (typeof value !== 'string' || !/^(?:[a-f0-9]{2}:){5}[a-f0-9]{2}$/i.test(value)) {
    throw new Error('The processor did not return a valid device identity.');
  }
  const result = value.toLowerCase();
  if (result === '00:00:00:00:00:00' || result === 'ff:ff:ff:ff:ff:ff') {
    throw new Error('The processor returned an invalid device identity.');
  }
  return result;
}

function flashCapacity(value) {
  const match = /^(\d+)(KB|MB)$/.exec(value || '');
  if (!match) throw new Error('Could not identify the installed flash memory.');
  return Number(match[1]) * (match[2] === 'MB' ? 1024 * 1024 : 1024);
}

function requireUnsecured(info, loader) {
  const flags = info?.parsedFlags;
  if (!flags || typeof flags.SECURE_DOWNLOAD_ENABLE !== 'boolean' || typeof flags.SECURE_BOOT_EN !== 'boolean' ||
      !Number.isInteger(info.flashCryptCnt) || info.flashCryptCnt < 0 || info.flashCryptCnt > 255) {
    throw new Error('Could not verify the processor security settings.');
  }
  const encrypted = ((info.flashCryptCnt.toString(2).match(/1/g) || []).length & 1) !== 0;
  if (loader.secureDownloadMode || flags.SECURE_DOWNLOAD_ENABLE || flags.SECURE_BOOT_EN || encrypted) {
    throw new Error('This installer cannot program a device with secure boot, flash encryption, or secure download enabled.');
  }
}

async function validateFiles(key, target, files) {
  const profile = profiles[key];
  if (!profile || target?.chip !== profile.chip || target.flashSizeBytes !== profile.capacity ||
      !Array.isArray(target.parts) || target.parts.length !== 3 || !Array.isArray(files) || files.length !== 3) {
    throw new Error('The release does not match the selected Cerberus processor.');
  }
  const offsets = key === 'c5' ? [0x2000, 0x8000, 0x10000] : [0, 0x8000, 0x10000];
  const sorted = [...files].sort((a, b) => a.part.offset - b.part.offset);
  for (let index = 0; index < sorted.length; index++) {
    const {part, data} = sorted[index];
    const approved = target.parts.find(item => item.offset === part.offset);
    const limit = index === 0 ? 0x8000 : index === 1 ? 0x9000 : key === 'c5' ? profile.capacity : 0x710000;
    if (!approved || part.offset !== offsets[index] || !Number.isInteger(part.size) || part.size <= 0 ||
        approved.size !== part.size || approved.md5 !== part.md5 || approved.sha256 !== part.sha256 ||
        !(data instanceof Uint8Array) || data.length !== part.size ||
        part.offset + Math.ceil(data.length / 4096) * 4096 > limit ||
        !/^[a-f0-9]{32}$/.test(part.md5) || !/^[a-f0-9]{64}$/.test(part.sha256)) {
      throw new Error('The firmware layout is invalid. No device was written.');
    }
    validateImage(key, part, data);
    if (md5Bytes(data) !== part.md5 || await sha256(data) !== part.sha256) {
      throw new Error('Firmware checksum mismatch. Reload the release before installing.');
    }
  }
  const erase = target.eraseRegions || [];
  if (!Array.isArray(erase) || erase.length > 1 ||
      erase.some(region => key !== 'c5' || region.offset !== 0 || region.size !== 4096)) {
    throw new Error('The release requests an unsupported erase operation.');
  }
  return sorted;
}

// These volatile register operations match Espressif's esp32s3.py implementation.
// Call only after the ROM has positively identified an unsecured ESP32-S3.
// Source: https://github.com/espressif/esptool/blob/master/esptool/targets/esp32s3.py
// prepareS3 follows disable_watchdogs; resetS3 follows watchdog_reset and the
// RTC_CNTL_FORCE_DOWNLOAD_BOOT clearing step in hard_reset. No eFuses are written.
async function prepareS3(loader) {
  if (loader.syncStubDetected) return;
  await loader.writeReg(0x600080b0, 0x50d83aa1);
  await loader.writeReg(0x60008098, 0);
  await loader.writeReg(0x600080b0, 0);
  await loader.writeReg(0x600080b8, 0x8f1d312a);
  const swd = await loader.readReg(0x600080b4);
  await loader.writeReg(0x600080b4, (swd | 0x80000000) >>> 0);
  await loader.writeReg(0x600080b8, 0);
}

async function resetS3(loader, sleep) {
  await loader.writeReg(0x6000812c, 0, 1);
  await loader.writeReg(0x600080b0, 0x50d83aa1);
  await loader.writeReg(0x6000809c, 2000);
  await loader.writeReg(0x60008098, 0xd0000102);
  await loader.writeReg(0x600080b0, 0);
  await sleep(500);
}

async function eraseRegion(loader, region) {
  const payload = new Uint8Array(8);
  const view = new DataView(payload.buffer);
  view.setUint32(0, region.offset, true);
  view.setUint32(4, region.size, true);
  await loader.checkCommand('erase region', loader.ESP_ERASE_REGION, payload, 0, 0,
    loader.timeoutPerMb(loader.ERASE_REGION_TIMEOUT_PER_MB, region.size));
}

export function createSerialApi({Loader = ESPLoader, TransportClass = Transport, sleep = wait,
  diagnosticDuration = 8000, identityDuration = 2000} = {}) {
  async function flashTarget({port, target, key, files, expectedMac, onLog = () => {}, onProgress = () => {}}) {
    // Validate all bytes before opening a device; a mutated download cannot reach erase.
    const ordered = await validateFiles(key, target, files);
    const profile = profiles[key];
    assertPort(port, key === 'c5' ? BRIDGE_PID : NATIVE_PID);
    const requiredMac = expectedMac ? canonicalMac(expectedMac) : null;
    const transport = new TransportClass(port, false);
    const terminal = {clean() {}, write: text => onLog(String(text)), writeLine: text => onLog(String(text))};
    const loader = new Loader({transport, baudrate: 115200, romBaudrate: 115200, terminal,
      serialOptions: {bufferSize: 65536, flowControl: 'none'}});
    let primaryError;
    let result;
    try {
      await loader.detectChip(key === 'c5' ? 'no_reset' : 'default_reset', 3);
      if (loader.chip?.CHIP_NAME !== profile.name || loader.chip?.IMAGE_CHIP_ID !== profile.id) {
        throw new Error(`Wrong processor selected. This step requires ${profile.name}.`);
      }
      const security = await loader.getSecurityInfo();
      requireUnsecured(security, loader);
      const mac = canonicalMac(await loader.chip.readMac(loader));
      if (requiredMac && mac !== requiredMac) throw new Error('This is a different Cerberus. Reconnect the board used for this installation.');
      onLog(`${profile.name} identified: ${mac}`);
      await loader.chip.postConnect?.(loader);
      if (key !== 'c5') await prepareS3(loader);
      // The bridge streams small UART chunks. Limit both upload and write bursts.
      if (key === 'c5') loader.ESP_RAM_BLOCK = 0x1000;
      await loader.runStub();
      if (!loader.IS_STUB) throw new Error('The required flash loader did not start. No firmware was written.');
      if (key === 'c5') loader.FLASH_WRITE_SIZE = 0x1000;
      await loader.flashSpiAttach(0);
      const flashSize = flashCapacity(await loader.detectFlashSize());
      if (flashSize < target.flashSizeBytes) throw new Error('This board has less flash memory than this firmware requires.');
      for (const {part, data} of ordered) {
        if (part.offset + Math.ceil(data.length / loader.FLASH_WRITE_SIZE) * loader.FLASH_WRITE_SIZE > flashSize) {
          throw new Error('The firmware does not fit the detected flash memory.');
        }
      }
      for (const region of target.eraseRegions || []) await eraseRegion(loader, region);
      await loader.writeFlash({
        fileArray: ordered.map(({part, data}) => ({address: part.offset, data})),
        flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep',
        eraseAll: false, compress: key !== 'c5', calculateMD5Hash: md5Bytes,
        reportProgress: (fileIndex, written, total) => onProgress(fileIndex, written, total),
      });
      // Compare against the release manifest, independently of loader buffer changes.
      for (const {part, data} of ordered) {
        const actual = await loader.flashMd5sum(part.offset, data.length);
        if (typeof actual !== 'string' || actual.toLowerCase() !== part.md5) {
          throw new Error('Firmware verification failed. Keep the board connected and retry this step.');
        }
      }
      onLog('All written firmware matched the release checksums.');
      result = {mac, chip: profile.chip, flashSize, resetError: null};
      if (key !== 'c5') {
        try {
          await resetS3(loader, sleep);
        } catch (error) {
          result.resetError = 'Firmware verified, but the board did not acknowledge restart. Release BOOT and press RESET.';
          onLog(`${result.resetError} ${error.message || error}`);
        }
      }
    } catch (error) {
      primaryError = error;
    } finally {
      try {
        await transport.disconnect();
      } catch (error) {
        // Restart normally removes the endpoint. Preserve the earlier flash result.
        onLog(`USB connection closed: ${error.message || error}`);
      }
    }
    if (primaryError) throw primaryError;
    return result;
  }

  async function bridgeControl(port, baud) {
    assertPort(port, BRIDGE_PID);
    if (baud !== 1200 && baud !== 2400) throw new Error('Unsupported bridge control request.');
    let opened = false;
    try {
      await port.open({baudRate: baud});
      opened = true;
      await sleep(baud === 2400 ? 1000 : 500);
    } finally {
      if (opened) {
        // The 1200-baud command intentionally removes this USB endpoint.
        try { await port.close(); } catch (error) {
          if (baud !== 1200) throw error;
        }
      }
    }
  }

  async function bridgeIdentity(port) {
    assertPort(port, BRIDGE_PID);
    let opened = false;
    let reader;
    let readTask;
    let deadlineTimer;
    let stopped = false;
    const failure = 'Could not identify this Cerberus bridge. Reconnect the original board before continuing.';
    try {
      // 9600 is a USB control request; it does not change the C5 UART baud rate.
      await port.open({baudRate: 9600, bufferSize: 4096});
      opened = true;
      reader = port.readable.getReader();
      readTask = (async () => {
        const decoder = new TextDecoder();
        let pending = '';
        let received = 0;
        try {
          while (!stopped && received <= 4096) {
            const {value, done} = await reader.read();
            if (done) break;
            received += value.length;
            if (received > 4096) break;
            pending += decoder.decode(value, {stream: true});
            let newline;
            while ((newline = pending.indexOf('\n')) !== -1) {
              const line = pending.slice(0, newline).replace(/\r$/, '');
              pending = pending.slice(newline + 1);
              const match = /^CERBERUS_BRIDGE_V1 s3_mac=((?:[a-f0-9]{2}:){5}[a-f0-9]{2})$/i.exec(line);
              // Preserve the exact protocol prefix; only hexadecimal MAC letters may vary in case.
              if (match && line.startsWith('CERBERUS_BRIDGE_V1 s3_mac=')) return canonicalMac(match[1]);
            }
          }
          throw new Error(failure);
        } finally { reader.releaseLock(); }
      })();
      const deadline = new Promise((_, reject) => {
        deadlineTimer = setTimeout(() => reject(new Error(`${failure} Identity response timed out.`)),
          Math.min(Math.max(identityDuration, 1), 2000));
      });
      return await Promise.race([readTask, deadline]);
    } finally {
      clearTimeout(deadlineTimer);
      stopped = true;
      if (reader) await reader.cancel().catch(() => {});
      if (readTask) await readTask.catch(() => {});
      if (opened) await port.close().catch(() => {});
    }
  }

  async function diagnose(port) {
    assertPort(port, NATIVE_PID);
    let reader;
    let readTask;
    let opened = false;
    let stop = false;
    let raw = '';
    let deadlineTimer;
    const decoder = new TextDecoder();
    try {
      // Opening at the ordinary baud rate is enough. Never toggle DTR/RTS here.
      await port.open({baudRate: 115200, bufferSize: 65536});
      opened = true;
      reader = port.readable.getReader();
      readTask = (async () => {
        try {
          while (!stop && raw.length < 65536) {
            const {value, done} = await reader.read();
            if (done) break;
            raw = (raw + decoder.decode(value, {stream: true})).slice(0, 65536);
          }
        } catch (error) {
          if (!stop) raw += `\nUSB diagnostic read stopped: ${error.message || error}\n`;
        } finally {
          reader.releaseLock();
        }
      })();
      const deadline = new Promise(resolve => {
        deadlineTimer = setTimeout(() => { stop = true; resolve('deadline'); },
          Math.min(Math.max(diagnosticDuration, 1), 8000));
      });
      const writer = port.writable.getWriter();
      try {
        const sent = writer.write(new TextEncoder().encode('\r\ninfo\r\nc5\r\nhealth\r\nbattery\r\n'));
        if (await Promise.race([sent, deadline]) === 'deadline') {
          void writer.abort().catch(() => {});
        }
      } finally { writer.releaseLock(); }
      await deadline;
      stop = true;
      await reader.cancel().catch(() => {});
      await readTask;
      return raw.slice(0, 65536);
    } finally {
      clearTimeout(deadlineTimer);
      stop = true;
      if (reader) await reader.cancel().catch(() => {});
      if (readTask) await readTask;
      if (opened) await port.close().catch(() => {});
    }
  }

  return {flashTarget, bridgeControl, bridgeIdentity, diagnose};
}

const api = createSerialApi();
export const flashTarget = api.flashTarget;
export const bridgeControl = api.bridgeControl;
export const bridgeIdentity = api.bridgeIdentity;
export const diagnose = api.diagnose;

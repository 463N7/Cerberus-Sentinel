import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createSerialApi, md5Bytes } from '../src/serial.js';

const mac = '14:63:93:36:79:18';
const hash = (type, value) => createHash(type).update(value).digest('hex');

function fixture(key = 'c5') {
  const c5 = key === 'c5';
  const files = (c5 ? [0x2000, 0x8000, 0x10000] : [0, 0x8000, 0x10000]).map(offset => {
    const data = new Uint8Array(32);
    if (offset === 0x8000) { data[0] = 0xaa; data[1] = 0x50; }
    else { data[0] = 0xe9; data[12] = c5 ? 23 : 9; }
    const part = {offset, size: data.length, md5: hash('md5', data), sha256: hash('sha256', data)};
    return {part, data};
  });
  return {key, files, target: {chip: c5 ? 'esp32c5' : 'esp32s3', flashSizeBytes: c5 ? 0x200000 : 0x1000000,
    parts: files.map(file => file.part), eraseRegions: c5 ? [{offset: 0, size: 4096}] : []},
  port: {getInfo: () => ({usbVendorId: 0x303a, usbProductId: c5 ? 0x4001 : 0x1001})}};
}

function harness(key = 'c5', overrides = {}) {
  const input = fixture(key);
  const calls = [];
  let instance;
  class MockTransport {
    constructor() { calls.push(['transport']); }
    async disconnect() { calls.push(['disconnect']); }
  }
  class MockLoader {
    constructor(options) {
      instance = this;
      this.options = options;
      this.FLASH_WRITE_SIZE = 0x4000;
      this.ESP_RAM_BLOCK = 0x1800;
      this.ESP_ERASE_REGION = 0xd1;
      this.ERASE_REGION_TIMEOUT_PER_MB = 30000;
      this.chip = {CHIP_NAME: key === 'c5' ? 'ESP32-C5' : 'ESP32-S3', IMAGE_CHIP_ID: key === 'c5' ? 23 : 9,
        readMac: async () => overrides.mac || mac, postConnect: async () => calls.push(['postConnect'])};
      if (overrides.chip) this.chip = {...this.chip, ...overrides.chip};
    }
    async detectChip(mode) { calls.push(['detect', mode]); }
    async getSecurityInfo() { return overrides.security || {flashCryptCnt: 0,
      parsedFlags: {SECURE_DOWNLOAD_ENABLE: false, SECURE_BOOT_EN: false}}; }
    async runStub() { calls.push(['stub', this.ESP_RAM_BLOCK]); this.IS_STUB = overrides.stub !== false; }
    async flashSpiAttach(arg) { calls.push(['attach', arg]); }
    async detectFlashSize() { calls.push(['capacity']); return overrides.capacity || (key === 'c5' ? '8MB' : '16MB'); }
    timeoutPerMb() { return 30000; }
    async checkCommand(name, opcode, payload) { calls.push(['erase', name, opcode, [...payload]]); }
    async writeFlash(options) {
      calls.push(['write', options, this.FLASH_WRITE_SIZE]);
      if (overrides.writeError) throw new Error(overrides.writeError);
    }
    async flashMd5sum(address) {
      calls.push(['verify', address]);
      return overrides.md5 || input.files.find(file => file.part.offset === address).part.md5;
    }
    async readReg(address) { calls.push(['readReg', address]); return 0; }
    async writeReg(...args) {
      calls.push(['writeReg', ...args]);
      if (overrides.resetError && args[0] === 0x6000812c) throw new Error('USB removed');
    }
  }
  const api = createSerialApi({Loader: MockLoader, TransportClass: MockTransport,
    sleep: async ms => calls.push(['sleep', ms])});
  return {input, calls, api, get loader() { return instance; }};
}

test('C5 uses fixed baud, small RAM/write chunks, selective erase, keep headers, and mandatory verification', async () => {
  const h = harness();
  const result = await h.api.flashTarget(h.input);
  assert.equal(result.mac, mac);
  assert.equal(result.flashSize, 8 * 1024 * 1024);
  assert.equal(h.loader.options.baudrate, 115200);
  assert.equal(h.loader.options.romBaudrate, 115200);
  assert.deepEqual(h.calls.find(c => c[0] === 'detect'), ['detect', 'no_reset']);
  assert.deepEqual(h.calls.find(c => c[0] === 'stub'), ['stub', 4096]);
  const write = h.calls.find(c => c[0] === 'write');
  assert.equal(write[2], 4096);
  assert.equal(write[1].compress, false);
  assert.equal(write[1].eraseAll, false);
  assert.equal(write[1].flashSize, 'keep');
  assert.equal(write[1].calculateMD5Hash(h.input.files[0].data), h.input.files[0].part.md5);
  assert.equal(h.calls.filter(c => c[0] === 'verify').length, 3);
  assert.equal(h.calls.some(c => c[0] === 'writeReg'), false);
  assert.deepEqual(h.calls.at(-1), ['disconnect']);
  assert.ok(h.calls.findIndex(c => c[0] === 'capacity') < h.calls.findIndex(c => c[0] === 'erase'));
});

for (const [name, overrides, pattern] of [
  ['wrong chip', {chip: {CHIP_NAME: 'ESP32-S3', IMAGE_CHIP_ID: 9}}, /Wrong processor/],
  ['wrong board', {mac: '14:63:93:36:79:19'}, /different Cerberus/],
  ['secure boot', {security: {flashCryptCnt: 0, parsedFlags: {SECURE_BOOT_EN: true, SECURE_DOWNLOAD_ENABLE: false}}}, /secure boot/],
  ['flash encryption', {security: {flashCryptCnt: 7, parsedFlags: {SECURE_BOOT_EN: false, SECURE_DOWNLOAD_ENABLE: false}}}, /flash encryption/],
  ['secure download', {security: {flashCryptCnt: 0, parsedFlags: {SECURE_DOWNLOAD_ENABLE: true, SECURE_BOOT_EN: false}}}, /secure download/],
  ['missing security information', {security: {parsedFlags: {}}}, /security settings/],
]) {
  test(`${name} is refused before uploading the stub or writing registers/flash`, async () => {
    const h = harness('c5', overrides);
    await assert.rejects(h.api.flashTarget({...h.input, expectedMac: mac}), pattern);
    assert.equal(h.calls.some(c => ['stub', 'erase', 'write', 'writeReg'].includes(c[0])), false);
    assert.deepEqual(h.calls.at(-1), ['disconnect']);
  });
}

for (const [name, overrides, pattern] of [
  ['missing stub', {stub: false}, /loader did not start/],
  ['undersized flash', {capacity: '1MB'}, /less flash/],
  ['unknown flash', {capacity: 'unknown'}, /identify.*flash/],
]) {
  test(`${name} cannot reach erase/write`, async () => {
    const h = harness('c5', overrides);
    await assert.rejects(h.api.flashTarget(h.input), pattern);
    assert.equal(h.calls.some(c => ['erase', 'write'].includes(c[0])), false);
    assert.deepEqual(h.calls.at(-1), ['disconnect']);
  });
}

test('failed target MD5 never restarts the S3 or reports success', async () => {
  const h = harness('s3', {md5: '0'.repeat(32)});
  await assert.rejects(h.api.flashTarget(h.input), /verification failed/);
  assert.equal(h.calls.some(c => c[0] === 'writeReg' && c[1] === 0x6000812c), false);
  assert.deepEqual(h.calls.at(-1), ['disconnect']);
});

test('loader verification error is preserved and the port is closed', async () => {
  const h = harness('s3', {writeError: 'MD5 mismatch from loader'});
  await assert.rejects(h.api.flashTarget(h.input), /MD5 mismatch/);
  assert.equal(h.calls.some(c => c[0] === 'writeReg' && c[1] === 0x6000812c), false);
  assert.deepEqual(h.calls.at(-1), ['disconnect']);
});

test('S3 uses the validated watchdog reset only after all images verify', async () => {
  const h = harness('s3');
  const result = await h.api.flashTarget(h.input);
  assert.equal(result.resetError, null);
  const resetIndex = h.calls.findIndex(c => c[0] === 'writeReg' && c[1] === 0x6000812c);
  const finalVerify = h.calls.findLastIndex(c => c[0] === 'verify');
  assert.ok(resetIndex > finalVerify);
  assert.deepEqual(h.calls.slice(resetIndex, resetIndex + 5), [
    ['writeReg', 0x6000812c, 0, 1], ['writeReg', 0x600080b0, 0x50d83aa1],
    ['writeReg', 0x6000809c, 2000], ['writeReg', 0x60008098, 0xd0000102], ['writeReg', 0x600080b0, 0],
  ]);
});

test('reset failure returns verified status with a manual-reset instruction', async () => {
  const h = harness('s3', {resetError: true});
  const result = await h.api.flashTarget(h.input);
  assert.match(result.resetError, /Firmware verified.*press RESET/);
});

test('mutated download and invalid erase requests fail before opening USB', async () => {
  const h = harness();
  h.input.files[0].data[20] ^= 1;
  await assert.rejects(h.api.flashTarget(h.input), /checksum mismatch/);
  assert.equal(h.calls.length, 0);
  const other = harness();
  other.input.target.eraseRegions = [{offset: 0x9000, size: 4096}];
  await assert.rejects(other.api.flashTarget(other.input), /unsupported erase/);
  assert.equal(other.calls.length, 0);
});

test('USB endpoint guard rejects an unrelated serial port before opening it', async () => {
  const h = harness();
  h.input.port.getInfo = () => ({usbVendorId: 0x10c4, usbProductId: 0xea60});
  await assert.rejects(h.api.flashTarget(h.input), /bridge USB/);
  assert.equal(h.calls.length, 0);
});

test('MD5 hashes only a typed-array view, not unrelated surrounding bytes', () => {
  const bytes = new Uint8Array([1, 2, 3, 4]);
  assert.equal(md5Bytes(bytes.subarray(1, 3)), hash('md5', new Uint8Array([2, 3])));
});

test('bridge controls use only approved baud requests and always close', async () => {
  const events = [];
  const port = {getInfo: () => ({usbVendorId: 0x303a, usbProductId: 0x4001}),
    open: async options => events.push(['open', options.baudRate]), close: async () => events.push(['close'])};
  const api = createSerialApi({sleep: async ms => events.push(['wait', ms])});
  await api.bridgeControl(port, 2400);
  assert.deepEqual(events, [['open', 2400], ['wait', 1000], ['close']]);
  await assert.rejects(api.bridgeControl(port, 115200), /Unsupported/);
  assert.equal(events.length, 3);
});

test('diagnostics send only read-only commands, never reset, collect output, and release streams', async () => {
  const events = [];
  const response = 'product=Cerberus Sentinel\r\nversion=0.5.0-dev\r\n';
  const port = {
    getInfo: () => ({usbVendorId: 0x303a, usbProductId: 0x1001}),
    open: async options => events.push(['open', options.baudRate]),
    close: async () => events.push(['close']),
    setSignals: async () => { throw new Error('Diagnostics must not reset the board'); },
    readable: new ReadableStream({start(controller) { controller.enqueue(new TextEncoder().encode(response)); }}),
    writable: new WritableStream({write(data) { events.push(['commands', new TextDecoder().decode(data)]); }}),
  };
  const api = createSerialApi({diagnosticDuration: 10});
  assert.equal(await api.diagnose(port), response);
  assert.deepEqual(events, [['open', 115200], ['commands', '\r\ninfo\r\nc5\r\nhealth\r\nbattery\r\n'], ['close']]);
  assert.equal(port.readable.locked, false);
  assert.equal(port.writable.locked, false);
});

function identityPort(chunks = [], end = true) {
  const events = [];
  const port = {
    getInfo: () => ({usbVendorId: 0x303a, usbProductId: 0x4001}),
    open: async options => events.push(['open', options.baudRate]),
    close: async () => events.push(['close']),
    setSignals: async () => { throw new Error('Identity query must not reset the board'); },
    readable: new ReadableStream({
      start(controller) {
        for (const chunk of chunks) controller.enqueue(new TextEncoder().encode(chunk));
        if (end) controller.close();
      },
      cancel() { events.push(['cancel']); },
    }),
  };
  return {port, events};
}

test('bridge identity reads the exact protocol response across USB chunks', async () => {
  const {port, events} = identityPort(['older UART output\r\nCERBERUS_BRIDGE_V1 s3_', 'mac=AA:BB:CC:12:', '34:56\r\n']);
  const api = createSerialApi({identityDuration: 20});
  assert.equal(await api.bridgeIdentity(port), 'aa:bb:cc:12:34:56');
  assert.deepEqual(events, [['open', 9600], ['close']]);
  assert.equal(port.readable.locked, false);
});

for (const response of [
  'OTHER_BRIDGE s3_mac=14:63:93:36:79:18\r\n',
  'cerberus_bridge_v1 s3_mac=14:63:93:36:79:18\r\n',
  'CERBERUS_BRIDGE_V1 s3_mac=14:63:93:36:79:18 untrusted\r\n',
  'CERBERUS_BRIDGE_V1 s3_mac=00:00:00:00:00:00\r\n',
  'CERBERUS_BRIDGE_V1 s3_mac=GG:63:93:36:79:18\r\n',
]) {
  test(`bridge refuses malformed identity: ${response.trim()}`, async () => {
    const {port, events} = identityPort([response]);
    const api = createSerialApi({identityDuration: 20});
    await assert.rejects(api.bridgeIdentity(port), /identify|invalid device identity/);
    assert.deepEqual(events.at(-1), ['close']);
    assert.equal(port.readable.locked, false);
  });
}

test('bridge identity timeout cancels the pending read and closes the USB port', async () => {
  const {port, events} = identityPort([], false);
  const api = createSerialApi({identityDuration: 10});
  await assert.rejects(api.bridgeIdentity(port), /Identity response timed out/);
  assert.deepEqual(events, [['open', 9600], ['cancel'], ['close']]);
  assert.equal(port.readable.locked, false);
});

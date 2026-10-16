const HASH = /^[a-f0-9]{64}$/;
const MD5 = /^[a-f0-9]{32}$/;
const profiles = {bridge: ['esp32s3', 9, 0, 16777216], s3: ['esp32s3', 9, 0, 16777216], c5: ['esp32c5', 23, 8192, 2097152]};

export function relativeUrl(path, base) {
  if (typeof path !== 'string' || !/^[a-zA-Z0-9_./-]+$/.test(path) || path.startsWith('/') || path.split('/').includes('..')) {
    throw new Error('The release contains an unsafe download path.');
  }
  const url = new URL(path, base);
  if (url.origin !== new URL(base).origin) throw new Error('Firmware must come from this installer.');
  return url.href;
}

export function validateManifest(manifest) {
  if (manifest.schemaVersion !== 1 || manifest.product !== 'Cerberus Sentinel' ||
      typeof manifest.version !== 'string' || !manifest.version || typeof manifest.releaseId !== 'string' ||
      !HASH.test(manifest.sourceSha256)) throw new Error('Unrecognized Cerberus release.');
  for (const [key, [chip, , bootOffset, budget]] of Object.entries(profiles)) {
    const target = manifest.targets?.[key];
    if (target?.chip !== chip || target.flashSizeBytes !== budget || target.parts?.length !== 3) {
      throw new Error(`Invalid ${key} hardware profile.`);
    }
    const allowed = [bootOffset, 0x8000, 0x10000];
    const ordered = [...target.parts].sort((a,b) => a.offset-b.offset);
    for (let i = 0; i < ordered.length; i++) {
      const p = ordered[i];
      const limit = i === 0 ? 0x8000 : i === 1 ? 0x9000 : key === 'c5' ? budget : 0x710000;
      if (p.offset !== allowed[i] || !Number.isInteger(p.size) || p.size <= 0 ||
          p.offset + Math.ceil(p.size / 4096) * 4096 > limit || !HASH.test(p.sha256) || !MD5.test(p.md5)) {
        throw new Error(`Invalid ${key} firmware layout or checksum.`);
      }
      relativeUrl(p.path, 'https://installer.invalid/firmware/release/manifest.json');
    }
    const erase = target.eraseRegions || [];
    if (erase.some(e => key !== 'c5' || e.offset !== 0 || e.size !== 4096) || erase.length > 1) {
      throw new Error('The release requests an unsupported erase operation.');
    }
  }
  return manifest;
}

export async function sha256(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2,'0')).join('');
}

export function validateImage(key, part, bytes) {
  if (bytes.length !== part.size) throw new Error(`${key}: incomplete firmware download.`);
  if (part.offset !== 0x8000) {
    const chipId = bytes[12] | (bytes[13] << 8);
    if (bytes[0] !== 0xe9 || bytes.length < 24 || chipId !== profiles[key][1]) {
      throw new Error(`${key}: firmware belongs to a different chip.`);
    }
  } else if (bytes[0] !== 0xaa || bytes[1] !== 0x50) {
    throw new Error(`${key}: invalid partition table.`);
  }
}

async function download(url) {
  const r = await fetch(url, {cache:'no-store', credentials:'omit'});
  if (!r.ok) throw new Error(`Firmware download failed (${r.status}). Refresh and try again.`);
  return new Uint8Array(await r.arrayBuffer());
}

export async function loadRelease(base, progress = () => {}) {
  const pointer = JSON.parse(new TextDecoder().decode(await download(new URL('firmware/latest.json', base))));
  if (!HASH.test(pointer.sha256)) throw new Error('Missing release checksum.');
  const manifestUrl = relativeUrl(pointer.manifest, new URL('firmware/latest.json',base));
  const raw = await download(manifestUrl);
  if (await sha256(raw) !== pointer.sha256) throw new Error('Release checksum does not match. Refresh before continuing.');
  const manifest = validateManifest(JSON.parse(new TextDecoder().decode(raw)));
  const files = {};
  let count = 0;
  // Every image is downloaded and validated before the first device write.
  for (const key of ['bridge','c5','s3']) {
    files[key] = [];
    for (const part of manifest.targets[key].parts) {
      const data = await download(relativeUrl(part.path, manifestUrl));
      validateImage(key,part,data);
      if (await sha256(data) !== part.sha256) throw new Error(`${key}: firmware checksum mismatch. No installation was started.`);
      files[key].push({part,data});
      progress(++count,9);
    }
  }
  return {manifest, files, manifestUrl};
}

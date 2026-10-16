export const SESSION_KEY = 'cerberus-install-v1';
export function freshSession(releaseId) {
  return {releaseId, startedAt:new Date().toISOString(), stage:'bridge', verified:{}, s3Mac:null, c5Mac:null, checks:null};
}
export function restoreSession(raw, releaseId) {
  try {
    const value = JSON.parse(raw);
    if (value?.releaseId !== releaseId || !['bridge','c5','s3','check','done'].includes(value.stage) || typeof value.verified !== 'object' || !value.verified) return freshSession(releaseId);
    const validMac = mac => mac === null || typeof mac === 'string' && /^(?:[a-f0-9]{2}:){5}[a-f0-9]{2}$/i.test(mac);
    if (!validMac(value.s3Mac) || !validMac(value.c5Mac) || typeof value.startedAt !== 'string') return freshSession(releaseId);
    const verified = {};
    for (const key of ['bridge','c5','s3']) {
      if (typeof value.verified[key] === 'string' && Number.isFinite(Date.parse(value.verified[key]))) verified[key] = value.verified[key];
    }
    // Historical flash receipts can resume work; startup success always needs a new check.
    return {releaseId, startedAt:value.startedAt, stage:value.stage === 'done' ? 'check' : value.stage,
      verified, s3Mac:value.s3Mac, c5Mac:value.c5Mac, checks:null};
  } catch { return freshSession(releaseId); }
}
export function markVerified(session, target, result) {
  if (!['bridge','c5','s3'].includes(target)) throw new Error('Invalid installation stage.');
  const updated = {...session, verified:{...session.verified, [target]:new Date().toISOString()}, checks:null};
  if (target === 'c5') updated.c5Mac = result.mac;
  else updated.s3Mac = result.mac;
  // Reinstalling the bridge starts a new radio/main installation attempt.
  if (target === 'bridge') { delete updated.verified.c5; delete updated.verified.s3; updated.c5Mac = null; }
  if (target === 'c5') delete updated.verified.s3;
  updated.stage = {bridge:'c5',c5:'s3',s3:'check'}[target];
  return updated;
}
export function startAttempt(session, target) {
  const updated={...session, stage:target, verified:{...session.verified}, checks:null};
  if(target === 'bridge') updated.verified={};
  if(target === 'c5') { delete updated.verified.c5; delete updated.verified.s3; }
  if(target === 's3') delete updated.verified.s3;
  return updated;
}
export function assessHealth(text, manifest, expectedMac) {
  const values = Object.fromEntries(text.split(/\r?\n/).filter(line=>/^[a-z0-9_]+=/.test(line)).map(line=>{
    const split=line.indexOf('=');return [line.slice(0,split),line.slice(split+1)];
  }));
  const checks = {
    identity: typeof expectedMac === 'string' && values.s3_mac?.toLowerCase() === expectedMac.toLowerCase(),
    firmware: values.version === manifest.version,
    display: /(?:^|\n)screen_buffered=yes\r?\n/.test(text),
    radio: /(?:^|\n)status=online\r?\n/.test(text) && new RegExp(`(?:^|\\n)firmware=${manifest.expectations?.c5Firmware ?? 31}\\r?\\n`).test(text),
    protocol: new RegExp(`(?:^|\\n)protocol=${manifest.expectations?.protocol ?? 2}\\r?\\n`).test(text),
    startup: /(?:^|\n)previous_reset_fault=no\r?\n/.test(text) && /(?:^|\n)crash_record=none\r?\n/.test(text),
  };
  return {passed:Object.values(checks).every(Boolean), checks};
}

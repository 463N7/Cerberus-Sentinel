import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateManifest, validateImage, relativeUrl, sha256, loadRelease} from '../src/release.js';
import {freshSession, restoreSession, markVerified, startAttempt, assessHealth} from '../src/session.js';

const base=new URL('../public/firmware/',import.meta.url);
const pointer=JSON.parse(await readFile(new URL('latest.json',base)));
const raw=await readFile(new URL(pointer.manifest,base));
const manifest=JSON.parse(raw);

test('shipped manifest and every image validate, exact SHA256 hashes',async()=>{
  assert.equal(await sha256(raw),pointer.sha256);
  validateManifest(manifest);
  for(const [key,target] of Object.entries(manifest.targets)) {
    for(const part of target.parts) {
      const bytes=new Uint8Array(await readFile(new URL(part.path,new URL(pointer.manifest,base))));
      validateImage(key,part,bytes);
      assert.equal(await sha256(bytes),part.sha256);
    }
  }
});
test('reject mixed chip, oversized image, NVS overwrite and unexpected erase',()=>{
  for(const mutate of [m=>m.targets.c5.chip='esp32s3',m=>m.targets.s3.parts[2].size=16000000,m=>m.targets.s3.parts[1].offset=0x9000,m=>m.targets.s3.eraseRegions=[{offset:0,size:16777216}]]) {
    const m=structuredClone(manifest);mutate(m);assert.throws(()=>validateManifest(m));
  }
});
test('reject download paths leaving the release origin/directory',()=>{
  for(const path of ['../secret','/secret','https://evil.test/a','a\\b','a/../b','%2e%2e/secret']) assert.throws(()=>relativeUrl(path,'https://good.test/firmware/r/manifest.json'));
});
test('reject wrong image chip before USB writes',()=>{
  const bytes=new Uint8Array(32);bytes[0]=0xe9;bytes[12]=23;
  assert.throws(()=>validateImage('s3',{size:32,offset:0},bytes));
  validateImage('c5',{size:32,offset:8192},bytes);
});
test('preflight refuses corrupted or missing later image even when bridge is valid',async()=>{
  const saved=globalThis.fetch;
  try {
    globalThis.fetch=async url=>{
      const pathname=new URL(url).pathname;
      const relative=pathname.replace('/installer/firmware/','');
      let bytes=await readFile(new URL(relative,base));
      if(relative.endsWith('/s3/app.bin')){bytes=Buffer.from(bytes);bytes[40]^=1;}
      return new Response(bytes);
    };
    await assert.rejects(loadRelease('https://good.test/installer/'),/checksum mismatch/);
  } finally {globalThis.fetch=saved;}
});
test('sessions do not carry success over to another release or a fresh bridge',()=>{
  const old=freshSession('old');old.stage='done';old.verified={c5:'time',s3:'time'};
  assert.equal(restoreSession(JSON.stringify(old),'new').stage,'bridge');
  const updated=markVerified(old,'bridge',{mac:'01:02:03:04:05:06'});
  assert.equal(updated.stage,'c5');assert.equal(updated.verified.c5,undefined);assert.equal(updated.verified.s3,undefined);
});
test('health needs full exact version/radio/protocol/display/startup evidence',()=>{
  const m={version:'0.5.0-dev',expectations:{protocol:2,c5Firmware:31}};
  const mac='02:03:04:05:06:07';
  const good=`s3_mac=${mac}\r\nversion=0.5.0-dev\r\nscreen_buffered=yes\r\nstatus=online\r\nfirmware=31\r\nprotocol=2\r\nprevious_reset_fault=no\r\ncrash_record=none\r\n`;
  assert.equal(assessHealth(good,m,mac).passed,true);
  for(const [a,b] of [['version=0.5.0-dev','version=0.5.0-dev-old'],['firmware=31','firmware=310'],['protocol=2','protocol=20'],['status=online','status=unavailable'],['screen_buffered=yes','screen_buffered=no'],['previous_reset_fault=no','previous_reset_fault=yes'],[mac,'02:03:04:05:06:08']]) assert.equal(assessHealth(good.replace(a,b),m,mac).passed,false);
  assert.equal(assessHealth('',m).passed,false);
});
test('reloading a completed session requires a new identity-bound startup check',()=>{
  const old=freshSession('release');old.stage='done';old.checks={passed:true};old.s3Mac='02:03:04:05:06:07';
  const resumed=restoreSession(JSON.stringify(old),'release');
  assert.equal(resumed.stage,'check');assert.equal(resumed.checks,null);
});
test('failed reflashing cannot reuse previous successful write receipts',()=>{
  const old=freshSession('release');old.stage='done';old.verified={bridge:'old',c5:'old',s3:'old'};old.checks={passed:true};
  const retry=startAttempt(old,'c5');
  assert.equal(retry.verified.c5,undefined);assert.equal(retry.verified.s3,undefined);assert.equal(retry.checks,null);
  const restored=markVerified(startAttempt(retry,'s3'),'s3',{mac:'02:03:04:05:06:07'});
  assert.equal(restored.verified.c5,undefined);
  const check=startAttempt(old,'check');assert.equal(check.stage,'check');assert.equal(check.checks,null);
});

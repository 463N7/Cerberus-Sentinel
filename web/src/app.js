import './style.css';
import {loadRelease, relativeUrl} from './release.js';
import {freshSession, restoreSession, markVerified, startAttempt, assessHealth, SESSION_KEY} from './session.js';
import {flashTarget, bridgeControl, bridgeIdentity, diagnose} from './serial.js';

const $ = id => document.getElementById(id);
const steps = ['bridge','c5','s3','check'];
let release, session, busy = false, supported = false, wakeLock;
const lines = [];
const descriptions = {
  bridge: {title:'Prepare your Cerberus',chip:'ESP32-S3',button:'Connect & prepare',description:'Install the temporary USB bridge that lets this browser reach the onboard radio.',instructions:'Connect Cerberus using a USB data cable. Select the USB JTAG/serial device.\nThe touchscreen will go blank while the bridge is running. Your final Sentinel firmware is installed in step 3.'},
  c5: {title:'Give your guardian its senses',chip:'ESP32-C5',button:'Connect & install radio',description:'The bridge now connects this browser to the onboard C5. This step may take several minutes.',instructions:'Keep the same Cerberus connected. Select the Cerberus C5 Bridge USB port when prompted.\nIf it is missing after step 1, tap S3 RESET with BOOT released, then try again.'},
  s3: {title:'Install the Sentinel experience',chip:'ESP32-S3',button:'Connect & install Sentinel',description:'Install the touchscreen firmware and restore normal operation.',instructions:'Select the USB JTAG/serial device for the same Cerberus.\nIf it has not returned: hold S3 BOOT, tap S3 RESET, then release BOOT. Keep the USB cable connected.'},
  check: {title:'Meet your guardian',chip:'STARTUP CHECK',button:'Connect & check startup',description:'Confirm the installed version, buffered display, and communication with the C5. This takes about eight seconds.',instructions:'Wait for the Sentinel home screen, then select its USB JTAG/serial device.\nIf the screen stays blank, tap S3 RESET with BOOT released. The final visual and touch checks are yours to confirm on the board.'},
  done: {title:'Your guardian is ready',chip:'CHECKS PASSED',button:'Check startup again',description:'Both firmware writes were verified, and Sentinel reports the expected version and an online C5.',instructions:'Check that the touchscreen responds and the display looks correct. You can now disconnect Cerberus.\nSave a report for this board, or start a new board below.'},
};

function log(text) {
  const clean = String(text).replace(/\x1b\[[0-9;]*m/g,'');
  lines.push(`${new Date().toISOString()} ${clean}`);
  if (lines.length > 1800) lines.splice(0, lines.length-1800);
  $('log').textContent = lines.join('\n');
  $('log').scrollTop = $('log').scrollHeight;
}
function status(text, kind='') { $('status').textContent = text; $('status').className = `status ${kind}`; }
function progress(value, label) { const p = Math.round(value); $('progress').value = p; $('percent').textContent = `${p}%`; $('progress-label').textContent = label; }
function save() { try { localStorage.setItem(SESSION_KEY, JSON.stringify(session)); } catch { log('Browser storage unavailable; keep this tab open to retain progress.'); } }
function render() {
  if (!session) return;
  const d = descriptions[session.stage];
  $('stage-title').textContent = d.title;
  $('stage-description').textContent = d.description;
  $('instructions').textContent = d.instructions;
  $('chip-label').textContent = d.chip;
  $('stage-label').textContent = session.stage === 'done' ? 'INSTALLATION COMPLETE' : `STEP 0${steps.indexOf(session.stage)+1} / 04`;
  $('primary').textContent = busy ? 'Working… keep connected' : `${d.button} →`;
  $('primary').disabled = busy || !supported || !$('consent').checked;
  $('consent-line').hidden = session.stage !== 'bridge' && $('consent').checked;
  for (const b of ['resume-c5','restore-s3','restart']) $(b).disabled = busy;
  $('consent').disabled = busy;
  for (const item of document.querySelectorAll('[data-stage]')) {
    const key = item.dataset.stage;
    const complete = key === 'check' ? session.stage === 'done' : !!session.verified[key];
    item.classList.toggle('complete',complete);
    item.classList.toggle('active',key === session.stage);
    item.querySelector('.step-state').textContent = complete ? '✓' : '';
    if (key === session.stage) item.setAttribute('aria-current','step'); else item.removeAttribute('aria-current');
  }
}
function showHealth(result) {
  const labels = {identity:'Same Cerberus board',firmware:'Sentinel version',display:'Buffered display',radio:'C5 online and current',protocol:'Processor communication',startup:'Clean startup'};
  $('check-results').replaceChildren();
  for (const [key, ok] of Object.entries(result.checks)) {
    const p = document.createElement('p');
    p.className = ok ? 'good' : 'bad';
    p.textContent = `${ok ? '✓' : '○'} ${labels[key]}${ok ? '' : ' — not confirmed'}`;
    $('check-results').append(p);
  }
  $('check-results').hidden = false;
}
async function run() {
  if (busy || !release || !supported || !$('consent').checked) return;
  const key = session.stage === 'done' ? 'check' : session.stage;
  session=startAttempt(session,key); save();
  busy = true; $('check-results').hidden=true; render();
  let port;
  try {
    // The chooser is called directly from the click, before asynchronous work.
    port = await navigator.serial.requestPort({filters:[{usbVendorId:0x303a,usbProductId:key === 'c5' ? 0x4001 : 0x1001}]});
    try { wakeLock = await navigator.wakeLock?.request('screen'); } catch { /* Optional; instructions also ask the user to keep the computer awake. */ }
    status(key === 'check' ? 'Reading startup status…' : 'Connecting and checking the selected processor…');
    progress(0, key === 'check' ? 'Checking startup' : 'Checking processor');
    log(`Starting ${key}; release ${release.manifest.releaseId}.`);
    if (key === 'check') {
      const output = await diagnose(port);
      log(output);
      const result = assessHealth(output,release.manifest,session.s3Mac);
      showHealth(result);
      session.checks = {...result, checkedAt:new Date().toISOString()};
      const bothVerified = !!session.verified.c5 && !!session.verified.s3;
      session.stage = result.passed && bothVerified ? 'done' : 'check';
      save(); progress(100,'Startup check finished');
      status(result.passed && bothVerified ? 'Both chips are installed and startup checks passed.' : result.passed ? 'Startup checks passed. This session has not verified both firmware writes; complete the full installation to record a complete result.' : 'Some checks were not confirmed. Wait for startup, then check again. Save the report if the issue continues.',result.passed ? 'success' : 'error');
      return;
    }
    if (key === 'c5') {
      status('Checking that the bridge belongs to this Cerberus…');
      const bridgeMac = await bridgeIdentity(port);
      if (session.s3Mac && bridgeMac !== session.s3Mac.toLowerCase()) throw new Error('This bridge belongs to a different Cerberus. Reconnect the original board.');
      session.s3Mac=bridgeMac; save();
      status('Preparing the C5 for installation…');
      await bridgeControl(port,2400);
    }
    const result = await flashTarget({port,key,target:release.manifest.targets[key],files:release.files[key],expectedMac:key === 'c5' ? session.c5Mac : session.s3Mac,onLog:log,onProgress:(fileIndex,written,total) => {
      progress((fileIndex + (total ? written / total : 0)) / 3 * 95,`Writing image ${fileIndex+1} of 3 · verification follows`);
    }});
    session = markVerified(session,key,result); save();
    progress(100,'Firmware written and verified');
    if (result.resetError) log(`Automatic reset could not be confirmed: ${result.resetError}`);
    if (key === 'c5') {
      status('Radio firmware verified. Returning USB control to the S3…');
      try { await bridgeControl(port,1200); }
      catch (e) { log(`USB handoff: ${e.message}. Use the S3 BOOT/RESET instructions.`); }
    }
    status(key === 'bridge' ? 'Bridge verified. Continue with the radio installation.' : key === 'c5' ? 'C5 verified. Continue with Sentinel installation; use BOOT/RESET if the S3 port is missing.' : 'Sentinel verified. Wait for the home screen, then run the startup check.','success');
  } catch (e) {
    const cancelled = e.name === 'NotFoundError';
    log(`${cancelled ? 'Port selection cancelled' : 'Stopped'}: ${e.message}`);
    status(cancelled ? 'No port selected. Nothing new was written.' : `${e.message} Use the recovery instructions below, then retry this step.`,cancelled ? '' : 'error');
    if (!cancelled) $('recovery').open = true;
  } finally {
    busy = false;
    try { await wakeLock?.release(); } catch { /* Already released by browser. */ }
    wakeLock = null;
    render();
  }
}
$('primary').addEventListener('click',run);
$('consent').addEventListener('change',render);
$('resume-c5').addEventListener('click',()=>{if(busy || !session)return;session.stage='c5';session.checks=null;$('check-results').hidden=true;save();render();status('Connect the same board with its bridge running. This step programs the C5.');});
$('restore-s3').addEventListener('click',()=>{if(busy || !session)return;session.stage='s3';session.checks=null;$('check-results').hidden=true;save();render();status('S3 recovery selected. This does not install or verify the C5.');});
$('restart').addEventListener('click',()=>{if(busy || !release)return;session=freshSession(release.manifest.releaseId);$('consent').checked=false;$('check-results').hidden=true;save();render();progress(0,'Ready for a new board');status('Connect one Cerberus to begin.');});
$('download-log').addEventListener('click',()=>{
  const report = {installerVersion:'0.1.0',savedAt:new Date().toISOString(),release:release?.manifest.releaseId || null,session:session || null,log:lines,qualification:'Browser checks do not replace physical display, touch and radio acceptance tests.'};
  const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));
  const a=document.createElement('a');a.href=url;a.download=`cerberus-install-${new Date().toISOString().replace(/[:.]/g,'-')}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
window.addEventListener('beforeunload',e=>{if(busy){e.preventDefault();e.returnValue='';}});

async function init() {
  supported = !!navigator.serial && window.isSecureContext;
  const policy = document.permissionsPolicy || document.featurePolicy;
  if (policy?.allowsFeature && !policy.allowsFeature('serial')) supported=false;
  if (!supported) {
    $('compatibility').hidden=false;
    $('compatibility').textContent='USB installation needs a secure page in desktop Chrome or Edge. Open this page directly in your browser, outside an embedded preview.';
  }
  try {
    release = await loadRelease(new URL('./',window.location.href),(n,total)=>progress(n/total*100,`Checking firmware ${n} of ${total}`));
    $('version').textContent=`Sentinel ${release.manifest.version}`;
    $('release-status').textContent='Complete set downloaded · SHA-256 checked';
    $('release-manifest').href=release.manifestUrl;$('release-manifest').hidden=false;
    let saved=null; try{saved=localStorage.getItem(SESSION_KEY);}catch{}
    session=restoreSession(saved,release.manifest.releaseId);
    if(session.stage!=='bridge') {log(`Resuming ${session.stage} from a saved local session. Reconfirm the same board before connecting.`);}
    const notice=document.createElement('a');notice.href=relativeUrl('WATCH_THIRD_PARTY_NOTICES.md',release.manifestUrl);notice.textContent='Firmware notices';$('release-status').append(' · ',notice);
    progress(0,'Ready to connect');status('Firmware downloads checked. Choose your board when ready.');
    render();
  } catch(e) {
    log(e.message);status(e.message,'error');$('version').textContent='Release unavailable';$('release-status').textContent='No device has been changed.';$('primary').disabled=true;
  }
}
init();

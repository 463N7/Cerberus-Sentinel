"""Verify the actual linked C5 adapter and vendor fallback; no RF is started."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from prepare_c5_raw_library import PIN,SYMBOL,ALIAS
from build_toolchain import resolve_build_tools

def verify(root: Path):
    build=root/'c5_firmware/.pio/build/c5-onboard'
    tool=resolve_build_tools(build,'esp32c5')
    elf=build/'firmware.elf'
    original=tool['idf']/'components/esp_wifi/lib/esp32c5/libnet80211.a'
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    if digest(original)!=PIN:raise RuntimeError('Unreviewed vendor archive')
    nm=subprocess.check_output([str(tool['nm']),str(elf)],text=True)
    symbols={name:int(address,16) for address,name in re.findall(r'^([0-9a-f]+) T (\w+)$',nm,re.M)}
    required=(SYMBOL,ALIAS,'esp_wifi_80211_tx','cerberus_c5_raw_deauth','cerberus_c5_raw_association')
    if any(n not in symbols for n in required) or len({symbols[n] for n in required})!=len(required):
        raise RuntimeError('Missing or overlapping raw TX symbols')
    disassembly={}
    for name in required:
        disassembly[name]=subprocess.check_output([str(tool['objdump']),
            '-d','--disassemble='+name,str(elf)],text=True)
    for caller,callee in [('esp_wifi_80211_tx',SYMBOL),(SYMBOL,ALIAS),('cerberus_c5_raw_deauth','esp_wifi_80211_tx'),('cerberus_c5_raw_association','esp_wifi_80211_tx')]:
        pattern=r'\b(?:jal|jalr)\b[^\n]*\b'+format(symbols[callee],'x')+r' <'+callee+r'>'
        if not re.search(pattern,disassembly[caller]):raise RuntimeError(f'Unverified call: {caller} -> {callee}')
    if any('<approved_frame>' not in disassembly[n] for n in [SYMBOL,'cerberus_c5_raw_deauth','cerberus_c5_raw_association']):
        raise RuntimeError('Current-buffer guard is absent')
    out=root/'artifacts/integration';out.mkdir(parents=True,exist_ok=True)
    (out/'c5-raw-link.txt').write_text('\n'.join(disassembly.values()))
    result={'status':'passed','elf_sha256':digest(elf),'vendor_archive_sha256':PIN,
        'tools':{name:str(path) for name,path in tool.items()},
        'addresses':{n:hex(symbols[n]) for n in required},'rf_verified':False}
    (out/'c5-raw-link.json').write_text(json.dumps(result,indent=2)+'\n')
    print('C5 ELF verified: TX -> guarded adapter -> original vendor fallback; SDK unchanged.')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args()
    verify(a.root)

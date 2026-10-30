"""Prepare a build-local C5 archive; never mutate the installed SDK.

Keep the vendor check callable and replace only its public binding. This avoids
the upstream global allow-multiple-definitions flag. Fail closed on SDK changes.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

PIN = "188d27c8da5fdeeba7feee6244435b133e3c5da5b38f9bbb6b9d87db7bc576ce"
SYMBOL = "ieee80211_raw_frame_sanity_check"
ALIAS = "cerberus_vendor_raw_frame_sanity_check"

def main():
    p=argparse.ArgumentParser()
    for name in ("source","output","ar","objcopy","nm"): p.add_argument("--"+name,required=True)
    a=p.parse_args(); source=Path(a.source); output=Path(a.output)
    original=hashlib.sha256(source.read_bytes()).hexdigest()
    if original!=PIN: raise SystemExit("Unreviewed C5 Wi-Fi library; raw TX preparation refused")
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as work:
        work=Path(work); archive=work/output.name; shutil.copyfile(source,archive)
        subprocess.run([a.ar,"x",str(archive),"ieee80211_output.o"],cwd=work,check=True)
        obj=work/"ieee80211_output.o"
        symbols=subprocess.check_output([a.nm,str(obj)],text=True)
        if f"00000000 T {SYMBOL}" not in symbols: raise SystemExit("Unexpected sanity-check layout")
        subprocess.run([a.objcopy,"--weaken-symbol="+SYMBOL,
            "--add-symbol="+ALIAS+"=.text."+SYMBOL+":0,global,function",str(obj)],check=True)
        symbols=subprocess.check_output([a.nm,str(obj)],text=True)
        if f"00000000 W {SYMBOL}" not in symbols or f"00000000 T {ALIAS}" not in symbols:
            raise SystemExit("Archive symbol preparation failed")
        subprocess.run([a.ar,"r",str(archive),str(obj)],check=True)
        shutil.copyfile(archive,output)
    if hashlib.sha256(source.read_bytes()).hexdigest()!=original:
        raise SystemExit("SDK archive unexpectedly changed")
    print("C5 build-local raw TX archive prepared; installed SDK unchanged")

if __name__=="__main__": main()

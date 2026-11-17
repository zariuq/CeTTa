#!/usr/bin/env python3
"""Check that changed linked kernel images are refused before execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();binary=a.binary.resolve();a.out.mkdir(parents=True,exist_ok=False)
raw=binary.read_bytes()
if raw[:6]!=b'\x7fELF\x02\x01':raise SystemExit('requires a little-endian ELF64 executable')
phoff=struct.unpack_from('<Q',raw,32)[0];phsize,phnum=struct.unpack_from('<HH',raw,54)
segments=[struct.unpack_from('<IIQQQQQQ',raw,phoff+i*phsize) for i in range(phnum)]
symbols={name:int(address,16) for address,kind,name in re.findall(r'(?m)^([0-9a-f]+) ([A-Za-z]) (\S+)$',subprocess.check_output(['nm','-n',str(binary)],text=True))}
def offset(name):
    address=symbols[name]
    for kind,flags,start,virtual,physical,size,memory,alignment in segments:
        if kind==1 and virtual<=address<virtual+size:return start+address-virtual
    raise AssertionError('symbol has no file span: '+name)
if 'tisa_run' in symbols:
    raise AssertionError('production executable contains the development t-ISA interpreter')
help_text=subprocess.check_output([str(binary),'--help'],text=True)
if '--kernel <c|x86|hybrid>' not in help_text:
    raise AssertionError('production kernel modes differ from the declared native interface')
rows=[]
for symbol in ['nm_unify_image','nm_emit_image','ck_pair_image','ck_observe_image','ck_pair_text','ck_observe_text','ck_constructor_image','ck_constructor_text','ck_word_image','ck_word_text','tc_batch_x86','tc_batch_x86_expected']:
    if symbol not in symbols:
        candidates=[n for n in symbols if ('unify' in n or 'emit' in n) and ('image' in n or 'entry' in n)]
        raise AssertionError((symbol,candidates))
    changed=bytearray(raw);at=offset(symbol);changed[at]^=1
    mutant=a.out/('changed-'+symbol);mutant.write_bytes(changed);shutil.copymode(binary,mutant)
    r=subprocess.run([str(mutant),'--kernel','x86','--help'],capture_output=True,text=True)
    if r.returncode!=2 or "error: kernel 'x86'" not in r.stderr:
        raise AssertionError((symbol,r.returncode,r.stdout[:100],r.stderr[:500]))
    rows.append({'symbol':symbol,'changed_offset':at,'exit':r.returncode,'refused':True,'diagnostic':r.stderr.strip()})
if binary.read_bytes()!=raw:raise AssertionError('qualification executable changed')
(a.out/'result.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(raw).hexdigest(),'results':rows},indent=2)+'\n')
print(f'PASS: {len(rows)} linked-image/text mutations refused before native execution')

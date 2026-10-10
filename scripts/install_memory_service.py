#!/usr/bin/env python3
"""Install the native collection worker and a dedicated persistent space owner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from install_space_service import atomic_text, select_link

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server',type=Path,required=True)
    p.add_argument('--worker',type=Path,required=True)
    p.add_argument('--prefix',type=Path,default=Path.home()/'.local')
    p.add_argument('--enable',action='store_true')
    p.add_argument('--dependent-service',action='append',default=[],
                   help='user service to order after the memory owner (with --enable)')
    a=p.parse_args()
    if a.dependent_service and not a.enable:raise ValueError('dependent services require --enable')
    if any(not re.fullmatch(r'[A-Za-z0-9_.@-]+\.service',name) for name in a.dependent_service):
        raise ValueError('invalid dependent service name')
    sources={'bin/cetta-space-service':a.server.resolve(),'bin/cetta-value-worker':a.worker.resolve()}
    for name in ['stdlib.metta','lib_tensor.metta','petta/lib_tensor.metta','petta/lib_retrieval.metta',
                 'petta/lib_memory_collection.metta','list.metta','petta/list.metta']:
        sources['lib/'+name]=ROOT/'lib'/name
    for name in ['space_service_client.py','value_worker_client.py','memory_collection.py']:
        sources['tools/'+name]=ROOT/'tools'/name
    hashes={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in sources.items()}
    identity=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest()[:24]
    base=a.prefix/'lib/cetta-memory';release=base/'releases'/identity
    release.mkdir(parents=True,exist_ok=True)
    for name,source in sources.items():
        target=release/name;target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()!=hashes[name]:
            raise ValueError('existing release was modified')
        if not target.exists():shutil.copy2(source,target)
    atomic_text(release/'manifest.json',json.dumps({'release':identity,'files':hashes},indent=2)+'\n',0o644)
    select_link(base/'current','releases/'+identity)
    if a.enable:
        if a.prefix!=Path.home()/'.local':raise ValueError('enable requires the user prefix')
        data=Path.home()/'.local/state/cetta-memory';data.mkdir(parents=True,exist_ok=True,mode=0o700)
        os.chmod(data,0o700)
        unit='''[Unit]
Description=CeTTa native memory spaces
After=default.target

[Service]
Type=simple
ExecStart=%h/.local/lib/cetta-memory/current/bin/cetta-space-service --socket %t/cetta-memory.sock --data %h/.local/state/cetta-memory --max-live-bytes 268435456 --max-query-bytes 134217728 --max-snapshot-bytes 268435456
Restart=on-failure
RestartSec=2
UMask=0077
NoNewPrivileges=true

[Install]
WantedBy=default.target
'''
        atomic_text(Path.home()/'.config/systemd/user/cetta-memory.service',unit,0o644)
        for name in a.dependent_service:
            dropin=Path.home()/'.config/systemd/user'/f'{name}.d/native-memory.conf'
            atomic_text(dropin,'[Unit]\nWants=cetta-memory.service\nAfter=cetta-memory.service\n',0o644)
        subprocess.run(['systemctl','--user','daemon-reload'],check=True)
        subprocess.run(['systemctl','--user','enable','--now','cetta-memory.service'],check=True)
    print(json.dumps({'release':str(release),'identity':identity}))

if __name__=='__main__':main()

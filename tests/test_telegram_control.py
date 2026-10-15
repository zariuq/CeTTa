#!/usr/bin/env python3
"""Operator control remains available without cognition and cannot retry sends."""
import json
import os
import pathlib
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
from test_telegram_transport import Peer, TOKEN, peer, certificate

BIN=str(pathlib.Path(sys.argv[1]).resolve())
CLI=str(pathlib.Path(sys.argv[2]).resolve())
ROOT=pathlib.Path(__file__).resolve().parents[1]

class Bot(Peer):
    def receipt(self, connection, method, path, body):
        data=json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock: self.calls.append(('poll',data['offset']))
            updates=[] if data['offset']>41 else [{'update_id':41,'message':{
                'message_id':8,'chat':{'id':42,'type':'private'},'from':{'id':7},'text':'synthetic'}}]
            return 200,[],json.dumps({'ok':True,'result':updates}).encode()
        assert path.endswith('/sendMessage')
        with self.lock: self.calls.append(('send',data['text']))
        # Remote acceptance followed by a lost response. Never retry this send.
        return None

def rpc(path, code=1, task='', body=''):
    with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as s:
        s.settimeout(4); s.connect(str(path))
        name=task.encode(); s.sendall(b'CWP1'+bytes((code,len(name)))+name+body.encode())
        r=s.recv(65606); assert r[:4]==b'CWP1'
        n=r[5]; return r[4],r[6:6+n].decode(),r[6+n:].decode()

def ctl(path, command, lost=False):
    with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as s:
        s.settimeout(4); s.connect(str(path)); s.sendall(b'CTC1'+command.encode())
        if lost: return
        r=s.recv(256); assert r[:4]==b'CTC1'; return r[4:].decode()

def wait(proc, predicate, what):
    end=time.monotonic()+15
    while time.monotonic()<end:
        if predicate(): return
        if proc.poll() is not None:
            out,err=proc.communicate(); assert TOKEN.split(':')[1] not in out+err
            raise AssertionError((what,proc.returncode,out,err))
        time.sleep(.03)
    proc.terminate(); out,err=proc.communicate(timeout=10)
    assert TOKEN.split(':')[1] not in out+err
    raise AssertionError((what,out,err))

def rows(state, space):
    try:
        with sqlite3.connect(f'file:{state}/journal.db?mode=ro',uri=True) as db:
            return dict(db.execute('SELECT key,value FROM records WHERE space=?',(space,)))
    except sqlite3.OperationalError: return {}

with tempfile.TemporaryDirectory(prefix='cetta-control-') as temp:
    root=pathlib.Path(temp)
    token=root/'token'; token.write_text(TOKEN); token.chmod(0o600)
    cert,key=certificate(root,'local','127.0.0.1')
    for mode in ('http/1.1','https/1.1','h2'):
        state=root/('state-'+mode.replace('/','-')); state.mkdir(mode=0o700)
        wp=root/(state.name+'.worker'); cp=root/(state.name+'.operator')
        worker=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        control=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        for s,p in ((worker,wp),(control,cp)):
            s.bind(str(p)); s.listen(16); p.chmod(0o600)
        tls=mode!='http/1.1'
        with peer('h2' if mode=='h2' else 'http/1.1',cert if tls else None,key if tls else None,Bot) as (bot,origin):
            base=[BIN,'--run','--root',str(ROOT),'--state-dir',str(state),'--worker','brain',
                '--chat','42','--credential-file',str(token),'--mock-origin',origin,
                '--listener-fd',str(worker.fileno())]
            if mode!='http/1.1': base+=['--ca-file',str(cert)]
            def start():
                return subprocess.Popen(base+['--operator-fd',str(control.fileno())],
                    pass_fds=(worker.fileno(),control.fileno()),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            alias=os.dup(worker.fileno())
            bad=subprocess.run(base+['--operator-fd',str(alias)],pass_fds=(worker.fileno(),alias),
                               capture_output=True,text=True,timeout=10)
            os.close(alias); assert bad.returncode==78 and not bot.calls,(bad.returncode,bad.stderr)
            cp.chmod(0o644)
            bad=subprocess.run(base+['--operator-fd',str(control.fileno())],pass_fds=(worker.fileno(),control.fileno()),
                               capture_output=True,text=True,timeout=10)
            cp.chmod(0o600); assert bad.returncode==78 and not bot.calls
            proc=start(); idle=[]
            try:
                wait(proc,lambda:bool(rows(state,'host.worker-ready')),'worker publication')
                code,task,_=rpc(wp); assert code==65
                assert ctl(cp,'status 42.0')=='waiting'
                assert ctl(cp,'receipt missing')=='unknown'
                # Packet bounds and protocol separation are enforced before decoding.
                for packet in (b'CTC1status 42.0'+b'x'*256,b'CWP1'+bytes((1,0)),b'CTC1'):
                    with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as bad:
                        bad.settimeout(4); bad.connect(str(cp)); bad.sendall(packet)
                        assert bad.recv(256)==b''
                for packet in (b'CTC1status 42.0\x00',b'CTC1status \xff'):
                    with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as bad:
                        bad.settimeout(4); bad.connect(str(cp)); bad.sendall(packet)
                        assert bad.recv(256)==b'CTC1invalid'
                with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as bad:
                    bad.settimeout(4); bad.connect(str(wp)); bad.sendall(b'CTC1release bad 42.0 fake')
                    assert bad.recv(256)==b'CWP1'+bytes((70,0))
                assert ctl(cp,'status 999.0')=='invalid'
                assert ctl(cp,'status 042.0')=='invalid'
                assert ctl(cp,'status 42.0.0')=='invalid'
                assert ctl(cp,'release bad 42.0 task extra')=='invalid'
                assert ctl(cp,'release r0 42.0 '+task)=='recorded'
                wait(proc,lambda:ctl(cp,'receipt r0')=='refused','waiting is not a hold')
                assert ctl(cp,'status 42.0')=='waiting'
                assert rpc(wp,2,task,'["cognitive-hold/1","model-outcome-unknown"]')[0]==66
                wait(proc,lambda:ctl(cp,'status 42.0').startswith('worker-held '+task+' '),'cognitive hold')
                for _ in range(3):
                    slow=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET); slow.connect(str(cp)); idle.append(slow)
                # Status works while some peers send nothing, and while no cognitive worker exists.
                check=subprocess.run([CLI,str(cp),'status','42.0'],capture_output=True,text=True,timeout=8)
                assert check.returncode==0 and ('worker-held '+task) in check.stdout,(check.returncode,check.stdout,check.stderr)
                polls=len(bot.calls); wait(proc,lambda:len(bot.calls)>polls,'polling with stalled operator peers')
                for slow in idle:
                    slow.settimeout(7); assert slow.recv(1)==b''; slow.close()
                idle.clear()
                assert ctl(cp,'release stale 42.0 different')=='recorded'
                wait(proc,lambda:ctl(cp,'receipt stale')=='refused','stale batch refusal')
                assert ctl(cp,'status 42.0').startswith('worker-held '+task+' ')
                ctl(cp,'release release1 42.0 '+task,lost=True)
                wait(proc,lambda:ctl(cp,'receipt release1')=='released','lost acknowledgment recovery')
                assert ctl(cp,'status 42.0')=='idle'
                before=rows(state,'telegram.controls')
                assert ctl(cp,'release release1 42.0 '+task)=='recorded'
                assert ctl(cp,'release release1 42.0 different')=='conflict'
                assert rows(state,'telegram.controls')==before
                assert not any(c[0]=='send' for c in bot.calls)
                proc.kill(); proc.communicate(timeout=5); proc=start()
                wait(proc,lambda:ctl(cp,'receipt release1')=='released','receipt survives restart')
                assert ctl(cp,'status 42.0')=='idle'
                # Give the same service a new input without touching its journal.
                bot.receipt_original=bot.receipt
                def next_input(connection,method,path,body):
                    data=json.loads(body)
                    if path.endswith('/getUpdates'):
                        with bot.lock: bot.calls.append(('poll',data['offset']))
                        updates=[] if data['offset']>42 else [{'update_id':42,'message':{
                            'message_id':9,'chat':{'id':42,'type':'private'},'from':{'id':7},'text':'next'}}]
                        return 200,[],json.dumps({'ok':True,'result':updates}).encode()
                    return bot.receipt_original(connection,method,path,body)
                bot.receipt=next_input
                wait(proc,lambda:bool(rows(state,'host.worker-ready')),'next task')
                code,task2,_=rpc(wp); assert code==65
                assert rpc(wp,2,task2,'[["send","one ambiguous send","plain"]]')[0]==66
                wait(proc,lambda:ctl(cp,'status 42.0').startswith('send-held '),'uncertain delivery')
                assert ctl(cp,'release refuse-send 42.0 '+task2)=='recorded'
                wait(proc,lambda:ctl(cp,'receipt refuse-send')=='refused','cannot release uncertain send')
                assert ctl(cp,'status 42.0').startswith('send-held ')
                assert bot.calls.count(('send','one ambiguous send'))==1
                assert len(rows(state,'host.outbox'))==3 # two worker tasks and one accepted send
            finally:
                for s in idle: s.close()
                if proc.poll() is None: proc.terminate()
                out,err=proc.communicate(timeout=10); assert TOKEN.split(':')[1] not in out+err
                assert proc.returncode==0,(proc.returncode,out,err)
                worker.close(); control.close()
        assert not bot.errors,bot.errors
        if mode=='h2': assert bot.negotiated and set(bot.negotiated)=={'h2'}
        print(mode+': independent operator status, exact hold release, receipt recovery and no-send-retry passed',flush=True)

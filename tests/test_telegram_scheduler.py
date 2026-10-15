#!/usr/bin/env python3
"""Recorded Telegram -> restarted worker -> ordered batch -> crash recovery."""
import collections
import json
import os
import pathlib
import select
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

from test_telegram_transport import Peer, TOKEN, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())
WORKER = r'''
import json,socket,sys
s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET); s.connect(sys.argv[1])
for line in sys.stdin:
    q=json.loads(line); name=q.get('id','').encode(); body=q.get('body','').encode()
    s.sendall(b'CWP1'+bytes([q['code'],len(name)])+name+body)
    r=s.recv(65606); assert r[:4]==b'CWP1'; n=r[5]
    print(json.dumps({'code':r[4],'id':r[6:6+n].decode(),'body':r[6+n:].decode()}),flush=True)
'''


class AgentPeer(Peer):
    def __init__(self,*args):
        self.updates=[]
        self.first=threading.Event(); self.release_first=threading.Event()
        self.second=threading.Event(); self.release_second=threading.Event()
        super().__init__(*args)
        self.add(41,42)

    def add(self,identifier,chat):
        with self.lock:
            self.updates.append({'update_id':identifier,'message':{'message_id':identifier,
                'chat':{'id':chat,'type':'private'},'from':{'id':7},'text':'synthetic input'}})

    def receipt(self,connection,method,path,body):
        assert method=='POST'
        data=json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock:
                self.calls.append(('poll',data['offset']))
                updates=[u for u in self.updates if u['update_id']>=data['offset']]
            return 200,[],json.dumps({'ok':True,'result':updates}).encode()
        assert path==f'/bot{TOKEN}/sendMessage'
        assert set(data)=={'chat_id','text'} and data['chat_id'] in (42,84)
        text=data['text']
        with self.lock: self.calls.append(('send',text))
        if text=='first':
            self.first.set(); assert self.release_first.wait(12)
        if text=='second':
            self.second.set(); assert self.release_second.wait(12)
            return None
        return 200,[],json.dumps({'ok':True,'result':{'message_id':99,'chat':{'id':data['chat_id']}}}).encode()


def records(db,space):
    try:
        with sqlite3.connect(f'file:{db}?mode=ro',uri=True) as c:
            return dict(c.execute('SELECT key,value FROM records WHERE space=?',(space,)))
    except sqlite3.OperationalError:
        return {}


def wait(proc,predicate,label,seconds=12):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if predicate(): return
        if proc.poll() is not None:
            out,err=proc.communicate(); assert TOKEN.split(':')[1] not in out+err
            raise AssertionError((label,proc.returncode,out,err))
        time.sleep(.025)
    raise AssertionError(label)


with tempfile.TemporaryDirectory(prefix='cetta-telegram-scheduler-') as temp:
    root=pathlib.Path(temp); token=root/'token'; token.write_text(TOKEN); token.chmod(0o600)
    cert,key=certificate(root,'local','127.0.0.1')
    for i,protocol in enumerate(('http/1.1','http/1.1','h2')):
        tls=i>0
        with peer(protocol,cert if tls else None,key if tls else None,AgentPeer) as (server,origin):
            db=root/f'state-{i}.db'; path=root/f'worker-{i}.sock'
            listener=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET); listener.bind(str(path)); listener.listen(16)
            services=[]; workers=[]

            def start(mode=None):
                p=subprocess.Popen([BIN,str(db),str(token),origin,str(cert) if tls else '-',str(listener.fileno())]+([mode] if mode else []),
                    pass_fds=(listener.fileno(),),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                services.append(p); return p

            def worker():
                p=subprocess.Popen([sys.executable,'-u','-c',WORKER,str(path)],stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                workers.append(p); return p

            def rpc(p,code,task='',body=''):
                p.stdin.write(json.dumps({'code':code,'id':task,'body':body})+'\n'); p.stdin.flush()
                assert select.select([p.stdout],[],[],5)[0], 'worker RPC timeout'
                line=p.stdout.readline(); assert line,(p.poll(),p.stderr.read())
                return json.loads(line)

            def stop(p,kill=False):
                p.send_signal(signal.SIGKILL if kill else signal.SIGTERM)
                out,err=p.communicate(timeout=8); assert TOKEN.split(':')[1] not in out+err
                assert p.returncode==(-signal.SIGKILL if kill else 0),(p.returncode,out,err)
                return out,err

            try:
                service=start('pause')
                wait(service,lambda:len(records(db,'host.worker-tasks'))==1,'first worker task')
                w=worker(); task=rpc(w,1); assert task['code']==65
                assert json.loads(task['body'])['message']['chat']['id']==42
                w.kill(); w.communicate(timeout=5)
                server.add(42,84)
                wait(service,lambda:len(records(db,'host.worker-tasks'))==2,'receive while worker stopped')
                w=worker(); resumed=rpc(w,1); assert resumed==task
                batch=json.dumps([['send',v,'plain'] for v in ('first','second','third')])
                assert rpc(w,2,task['id'],batch)['code']==66
                assert select.select([service.stdout],[],[],8)[0], 'acceptance pause timeout'
                assert service.stdout.readline().strip()=='paused-after-accept'
                assert not server.first.is_set()
                stop(service,True); w.kill(); w.communicate(timeout=5)
                service=start(); w=worker()
                wait(service,server.first.is_set,'first action')
                server.add(43,42)
                other=rpc(w,1); assert other['code']==65 and other['id']!=task['id']
                assert rpc(w,2,other['id'],json.dumps([['send','other','plain']]))['code']==66
                wait(service,lambda:('send','other') in server.calls,'unrelated chat progresses')
                assert ('send','second') not in server.calls and ('send','third') not in server.calls
                server.release_first.set(); wait(service,server.second.is_set,'second action only after first completion')
                stop(service,True); server.release_second.set()
                w.kill(); w.communicate(timeout=5)
                service=start()
                wait(service,lambda:b'tg-agent:held' in records(db,'host.actors').get('telegram/bot/42.0',b''),'recovered uncertainty')
                assert not records(db,'host.worker-ready')
                server.add(44,84)
                wait(service,lambda:len(records(db,'host.worker-tasks'))==3,'other chat after service restart')
                w=worker(); task2=rpc(w,1); assert task2['code']==65
                assert rpc(w,2,task2['id'],json.dumps([['send','after-restart','plain']]))['code']==66
                wait(service,lambda:('send','after-restart') in server.calls,'delivery after restart')
                time.sleep(1.2)
                out,err=stop(service)
                stats=dict(item.split('=') for item in out.strip().split())
                assert int(stats['evaluations'])<=12,stats
                assert int(stats['faults'])==0 and int(stats['transport_faults'])==0,(stats,err)
                counts=collections.Counter(value for kind,value in server.calls if kind=='send')
                assert counts=={'first':1,'second':1,'other':1,'after-restart':1},counts
                assert b'tg-agent:held' in records(db,'host.actors')['telegram/bot/42.0']
                assert 'bot/42.0/43' in records(db,'host.inbox')
                w.kill(); w.communicate(timeout=5)
                # Queues of one input and one effect are backpressure, not a
                # fault: two updates arriving together are both read, and the
                # batch is committed whole and its sends go out in order, once
                # each, through the one place.
                db=root/f'quota-{i}.db'
                with server.lock: server.updates=[]
                server.add(100,84); server.add(101,42)
                service=start('quota')
                wait(service,lambda:len(records(db,'host.worker-tasks'))==2,'quota worker tasks')
                w=worker(); task3=rpc(w,1); assert task3['code']==65
                assert rpc(w,2,task3['id'],json.dumps([['send',v,'plain'] for v in ('quota-0','quota-1','quota-2')]))['code']==66
                wait(service,lambda:('send','quota-2') in server.calls,'sends through a queue of one')
                assert sum(b'telegram.action' in v for v in records(db,'host.outbox').values())==3
                time.sleep(.1); out,err=stop(service)
                assert 'faults=0 ' in out,(out,err)
                sends=[v for k,v in server.calls if k=='send' and v.startswith('quota-')]
                assert sends==['quota-0','quota-1','quota-2'],sends
                w.kill(); w.communicate(timeout=5)
                print(('HTTPS' if tls else 'HTTP')+'/'+protocol+': worker restart, accepted-unsent recovery, independent chat, order, killed send, parked policy and backpressure at capacity passed',flush=True)
            finally:
                server.release_first.set(); server.release_second.set()
                for p in workers+services:
                    if p.poll() is None:
                        p.kill(); p.communicate(timeout=8)
                listener.close()

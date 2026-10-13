"""One Linux event watcher for configured directory-to-native-mailbox bindings."""
from __future__ import annotations
import argparse
import ctypes
import errno
import json
import os
from pathlib import Path
import select
import stat
import struct
import tempfile
import time
import uuid

if __package__:
    from .native_mailbox import Mailbox, MailboxError, text, validate_headers
    from .space_service_client import S, ServiceError, Uncertain
    from .value_worker_client import WorkerUnavailable
else:
    from native_mailbox import Mailbox, MailboxError, text, validate_headers
    from space_service_client import S, ServiceError, Uncertain
    from value_worker_client import WorkerUnavailable


class DeliveryUncertain(OSError):
    published=True


def deliver(directory, identity, correlation, sender, body, *, headers=None):
    """Publish a complete file by fsync + rename; destination is configuration."""
    directory=Path(directory); directory.mkdir(parents=True,exist_ok=True)
    value={"id":text(identity),"correlation":text(correlation),"sender":text(sender),"body":body}
    if headers is not None: value["headers"]=validate_headers(headers)
    if not isinstance(body,str):
        raise ValueError("text mailbox body required")
    fd,path=tempfile.mkstemp(prefix=".delivery-",dir=directory)
    published=False
    try:
        # A setgid mailbox directory gives each writer the receiver's group.
        # Let that receiver read the newly published envelope across accounts.
        os.fchmod(fd,0o640)
        with os.fdopen(fd,"w") as file:
            json.dump(value,file,ensure_ascii=False); file.write("\n"); file.flush(); os.fsync(file.fileno())
        target=directory/(uuid.uuid4().hex+".json")
        os.rename(path,target)
        published=True
        parent=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
        try: os.fsync(parent)
        finally: os.close(parent)
        return target
    except OSError as error:
        if published: raise DeliveryUncertain("message was published but durability acknowledgement failed: "+str(error)) from error
        raise
    finally:
        if os.path.exists(path): os.unlink(path)


def health(bridge, path):
    """Inspectable counters for held deliveries; contains no message bodies."""
    if not path: return
    target=Path(path); target.parent.mkdir(parents=True,exist_ok=True)
    value={"version":1,"observed_at":time.time(),"ingested":bridge.ingested,"held":bridge.held,
           "retrying":len(bridge.retry),"diagnostics":len(bridge.diagnostics),
           "reasons":sorted(set(bridge.diagnostics.values()))}
    fd,temporary=tempfile.mkstemp(prefix=".mailbox-health-",dir=target.parent)
    try:
        with os.fdopen(fd,"w") as stream:
            json.dump(value,stream); stream.write("\n"); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary,target)
        parent=os.open(target.parent,os.O_RDONLY|os.O_DIRECTORY)
        try: os.fsync(parent)
        finally: os.close(parent)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)


class DirectoryBridge:
    CLOSE_WRITE=0x8; MOVED_TO=0x80; CREATE=0x100
    DELETE_SELF=0x400; MOVE_SELF=0x800; OVERFLOW=0x4000; IGNORED=0x8000
    def __init__(self, mailbox, bindings, max_file_bytes=256*1024):
        if not isinstance(max_file_bytes,int) or max_file_bytes<1:
            raise ValueError("positive mailbox file byte budget required")
        self.mailbox=mailbox; self.bindings=[]; self.max_file_bytes=max_file_bytes
        self.lib=ctypes.CDLL(None,use_errno=True)
        self.lib.inotify_init1.argtypes=[ctypes.c_int]; self.lib.inotify_init1.restype=ctypes.c_int
        self.lib.inotify_add_watch.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_uint32]
        self.lib.inotify_add_watch.restype=ctypes.c_int
        self.lib.inotify_rm_watch.argtypes=[ctypes.c_int,ctypes.c_int]
        self.lib.inotify_rm_watch.restype=ctypes.c_int
        self.fd=self.lib.inotify_init1(os.O_NONBLOCK|os.O_CLOEXEC)
        if self.fd<0: raise OSError(ctypes.get_errno(),"inotify_init1")
        self.watches={}; self.diagnostics={}; self.ingested=0; self.held=0; self.overflows=0
        self.retry={}; self.retry_due=None; self.retry_delay=.1
        try:
            names=set()
            for binding in bindings:
                name=text(binding["name"]); sender=text(binding["sender"])
                if name in names: raise ValueError("duplicate mailbox binding")
                names.add(name)
                directory=Path(binding["directory"]).expanduser().absolute()
                directory.mkdir(parents=True,exist_ok=True)
                self.bindings.append({"name":name,"sender":sender,"directory":directory})
            self.arm()
            self.scan()  # subscribe before the authoritative directory check
        except BaseException:
            self.close(); raise

    def close(self):
        if self.fd>=0:
            os.close(self.fd); self.fd=-1

    def arm(self):
        active={}
        for binding in self.bindings:
            directory=binding["directory"]
            for path in (directory.parent,directory):
                if not path.exists(): continue
                mask=self.CLOSE_WRITE|self.MOVED_TO|self.CREATE|self.DELETE_SELF|self.MOVE_SELF
                wd=self.lib.inotify_add_watch(self.fd,os.fsencode(path),mask)
                if wd<0: raise OSError(ctypes.get_errno(),"inotify_add_watch")
                active[wd]=path
        for wd in self.watches.keys()-active.keys():
            self.lib.inotify_rm_watch(self.fd,wd)
        self.watches=active

    def _ingest(self,binding,path):
        fd=os.open(path,os.O_RDONLY|os.O_NONBLOCK|os.O_NOFOLLOW)
        try:
            before=os.fstat(fd)
            if not stat.S_ISREG(before.st_mode) or before.st_size>self.max_file_bytes:
                raise ValueError("mailbox file type or byte budget")
            with os.fdopen(fd,"rb",closefd=False) as file:
                raw=file.read(self.max_file_bytes+1)
            after=os.fstat(fd)
            if len(raw)>self.max_file_bytes or (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
                raise ValueError("mailbox file changed during read")
            value=json.loads(raw)
            required={"id","correlation","sender","body"}
            if not isinstance(value,dict) or not required<=set(value) or set(value)-required-{"headers"}:
                raise ValueError("mailbox envelope requires id, correlation, sender and body; use mailbox-post NAME --body-file FILE")
            if value["sender"]!=binding["sender"]:
                raise ValueError("mailbox sender differs from binding")
            # Exact structured occurrence identity, without a lossy hash or
            # filename authority. Equal IDs on different channels stay distinct.
            identity=json.dumps([binding["name"],value["sender"],text(value["id"])],
                                ensure_ascii=False,separators=(",",":"))
            options={"headers":validate_headers(value["headers"])} if "headers" in value else {}
            self.mailbox.send(identity,binding["name"],text(value["correlation"]),
                              value["sender"],value["body"],**options)
            current=path.lstat()
            if (current.st_dev,current.st_ino,current.st_size,current.st_mtime_ns)!=(
                    before.st_dev,before.st_ino,after.st_size,after.st_mtime_ns):
                raise ValueError("mailbox source replaced before acknowledgment")
            archive=binding["directory"]/"received"
            archive.mkdir(exist_ok=True)
            os.rename(path,archive/(uuid.uuid4().hex+".json"))
            for directory in (archive,binding["directory"]):
                parent=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
                try: os.fsync(parent)
                finally: os.close(parent)
            self.ingested+=1
        finally:
            os.close(fd)

    def scan(self):
        for binding in self.bindings:
            directory=binding["directory"]
            try: paths=list(directory.iterdir())
            except FileNotFoundError: continue
            for path in paths:
                if path.name.startswith(".") or not path.is_file() and not path.is_symlink(): continue
                self.ingest(binding,path)

    def ingest(self,binding,path):
        key=str(path)
        try:
            self._ingest(binding,path)
            self.diagnostics.pop(key,None); self.retry.pop(key,None)
        except FileNotFoundError:
            self.retry.pop(key,None)
        except Exception as error:
            # A malformed occurrence remains inspectable and cannot stop
            # other channels. Transport uncertainty is retried by its exact
            # durable message identity, never by replaying a mutation plan.
            self.held+=1; self.diagnostics[key]=type(error).__name__+": "+str(error)
            transient=(isinstance(error,(Uncertain,WorkerUnavailable)) or
                       isinstance(error,OSError) and error.errno!=errno.ELOOP or
                       isinstance(error,ServiceError) and error.reason in (S("space-limit"),S("stale-revision"),S("snapshot-limit")) or
                       isinstance(error,MailboxError) and str(error).startswith("mailbox contention"))
            if transient:
                self.retry[key]=(binding,path)
                if self.retry_due is None:
                    self.retry_due=time.monotonic()+self.retry_delay
            else:
                self.retry.pop(key,None)
                # Retain rejected bytes (or the refused symlink itself)
                # outside the watched input set so restart cannot retry it.
                try:
                    rejected=binding["directory"]/"rejected"
                    rejected.mkdir(exist_ok=True)
                    target=rejected/(uuid.uuid4().hex+path.suffix)
                    os.rename(path,target)
                    explanation={"source":path.name,"reason":self.diagnostics[key],
                        "usage":"mailbox-post NAME --body-file FILE",
                        "example":{"id":"unique-message-id","correlation":"unique-message-id","sender":binding["sender"],"body":"Your message"}}
                    reason=target.with_name(target.name+".reason")
                    with reason.open("w") as file:
                        json.dump(explanation,file,ensure_ascii=False);file.write("\n");file.flush();os.fsync(file.fileno())
                    for directory in (rejected,binding["directory"]):
                        parent=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
                        try: os.fsync(parent)
                        finally: os.close(parent)
                except FileNotFoundError:
                    pass
                except OSError as refusal:
                    self.diagnostics[key]+="; quarantine: "+type(refusal).__name__

    def rescan(self):
        """Recovery after overflow/reconnect: atomic publications are authority."""
        self.arm(); self.scan()

    def poll(self,timeout=None):
        if self.retry_due is not None:
            remaining=max(0,self.retry_due-time.monotonic())
            timeout=remaining if timeout is None else min(timeout,remaining)
        events=select.select([self.fd],[],[],timeout)[0]
        if not events:
            if self.retry_due is None or time.monotonic()<self.retry_due: return False
            try: self.mailbox.reconnect()
            except (OSError,Uncertain):
                self.retry_delay=min(5,self.retry_delay*2)
                self.retry_due=time.monotonic()+self.retry_delay
                return False
            self.retry_due=None
            for binding,path in list(self.retry.values()): self.ingest(binding,path)
            self.retry_delay=min(5,self.retry_delay*2) if self.retry else .1
            if self.retry: self.retry_due=time.monotonic()+self.retry_delay
            return True
        changed=False; rescan=False; arrivals={}
        while True:
            try: data=os.read(self.fd,65536)
            except BlockingIOError: break
            if not data: break
            offset=0
            while offset+16<=len(data):
                wd,mask,cookie,length=struct.unpack_from("iIII",data,offset)
                name=os.fsdecode(data[offset+16:offset+16+length].split(b"\0",1)[0])
                offset+=16+length
                if mask&self.OVERFLOW:
                    self.overflows+=1; rescan=True
                watched=self.watches.get(wd)
                if mask&self.IGNORED: self.watches.pop(wd,None)
                for binding in self.bindings:
                    directory=binding["directory"]
                    if watched==directory:
                        if mask&(self.DELETE_SELF|self.MOVE_SELF|self.IGNORED): rescan=True
                        if mask&(self.CLOSE_WRITE|self.MOVED_TO) and name and not name.startswith("."):
                            arrivals[str(directory/name)]=(binding,directory/name)
                    elif watched==directory.parent and name==directory.name:
                        if mask&(self.CREATE|self.MOVED_TO): rescan=True
        if rescan:
            self.rescan(); changed=True
        else:
            for binding,path in arrivals.values(): self.ingest(binding,path); changed=True
        return changed


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",required=True)
    args=parser.parse_args()
    config=json.loads(Path(args.config).read_text())
    box=Mailbox(config["socket"],config["space"],config["worker"],expected=config.get("storage_identity"))
    bridge=DirectoryBridge(box,config["bindings"],config.get("max_file_bytes",256*1024))
    try:
        health(bridge,config.get("health_path"))
        while True:
            bridge.poll()
            health(bridge,config.get("health_path"))
    except KeyboardInterrupt:
        pass
    finally:
        bridge.close(); box.close()


if __name__=="__main__": main()

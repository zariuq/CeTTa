"""Unix WebSocket App Server fixture; never starts model or tool work."""
import base64
import hashlib
import json
import socket
import struct
import threading


class WireAppServer:
    def __init__(self,path):
        self.path=path; self.calls=[]; self.errors=[]; self.turns=[]
        self.peer=None; self.lock=threading.Lock(); self.stopped=threading.Event()
        self.listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        self.listener.bind(str(path)); self.listener.listen(); self.listener.settimeout(.1)
        self.thread=threading.Thread(target=self.serve,daemon=True); self.thread.start()

    def close(self):
        self.stopped.set(); self.listener.close()
        with self.lock:
            if self.peer is not None:
                try: self.peer.shutdown(socket.SHUT_RDWR)
                except OSError: pass
        self.thread.join(timeout=2)
        if self.thread.is_alive(): raise AssertionError("wire fixture did not stop")
        if self.errors: raise AssertionError(self.errors)

    @staticmethod
    def exact(peer,count):
        value=bytearray()
        while len(value)<count:
            packet=peer.recv(count-len(value))
            if not packet: raise EOFError()
            value.extend(packet)
        return bytes(value)

    def receive(self,peer):
        first,second=self.exact(peer,2)
        if first&15==8: raise EOFError()
        if first!=0x81 or not second&0x80: raise AssertionError("masked JSON client frame required")
        size=second&0x7f
        if size==126: size=struct.unpack("!H",self.exact(peer,2))[0]
        elif size==127: size=struct.unpack("!Q",self.exact(peer,8))[0]
        mask=self.exact(peer,4); payload=self.exact(peer,size)
        return json.loads(bytes(value^mask[i%4] for i,value in enumerate(payload)))

    def send(self,value):
        payload=json.dumps(value,separators=(",",":")).encode()
        size=len(payload)
        if size<126: header=bytes((0x81,size))
        elif size<65536: header=bytes((0x81,126))+struct.pack("!H",size)
        else: header=bytes((0x81,127))+struct.pack("!Q",size)
        with self.lock: self.peer.sendall(header+payload)

    def complete(self,turn):
        self.turns=[turn]
        summary=dict(turn,itemsView="summary")
        self.send({"method":"turn/completed","params":{"threadId":"thread","turn":summary}})

    def handle(self,peer):
        headers=bytearray()
        while not headers.endswith(b"\r\n\r\n"): headers.extend(self.exact(peer,1))
        key=next(line.split(b":",1)[1].strip() for line in headers.split(b"\r\n")
                 if line.lower().startswith(b"sec-websocket-key:"))
        accept=base64.b64encode(hashlib.sha1(key+b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
        peer.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     b"Connection: Upgrade\r\nSec-WebSocket-Accept: "+accept+b"\r\n\r\n")
        while not self.stopped.is_set():
            request=self.receive(peer); method=request["method"]; params=request.get("params",{})
            self.calls.append((method,params))
            if method=="initialized": continue
            if method=="initialize": result={"userAgent":"mailbox-fixture"}
            elif method=="thread/resume":
                if params!={"threadId":"thread"}: raise AssertionError("subscription changed configuration")
                result={"thread":{"id":"thread"}}
            elif method=="thread/read":
                if params!={"threadId":"thread","includeTurns":True}: raise AssertionError("unexpected transcript query")
                result={"thread":{"id":"thread","turns":self.turns}}
            else: raise AssertionError("bridge started or steered model work: "+method)
            self.send({"id":request["id"],"result":result})

    def serve(self):
        while not self.stopped.is_set():
            try: peer,address=self.listener.accept()
            except socket.timeout: continue
            except OSError:
                if self.stopped.is_set(): break
                raise
            with self.lock: self.peer=peer
            try: self.handle(peer)
            except (EOFError,ConnectionError): pass
            except OSError as error:
                if not self.stopped.is_set(): self.errors.append(str(error))
            except Exception as error: self.errors.append(repr(error))
            finally:
                with self.lock: self.peer=None
                peer.close()

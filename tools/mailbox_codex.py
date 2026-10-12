"""Configured Codex turn completions to native mailboxes, driven by events.

The operator supplies an installed AppServer transport helper. Subscriptions
use thread/resume without overrides. No turns, prompts or model calls are
started here. Transcripts reconcile missed notifications after reconnect.
"""
from __future__ import annotations
import argparse
import importlib.util
from importlib.machinery import SourceFileLoader
import json
from pathlib import Path
import sys
import time

if __package__:
    from .native_mailbox import Mailbox, text
    from .space_service_client import Uncertain
else:
    from native_mailbox import Mailbox, text
    from space_service_client import Uncertain


class TranscriptPending(OSError):
    """A known completion is not yet available as a full transcript."""


class CodexBridge:
    TERMINAL={"completed","failed","interrupted"}

    def __init__(self,mailbox,bindings):
        self.mailbox=mailbox; self.bindings=[]; self.client=None; self.seen=set()
        self.pending=set()
        routes=set()
        for item in bindings:
            binding={key:text(item[key]) for key in ("thread","turn","channel","sender")}
            route=tuple(binding.values())
            if route in routes: raise ValueError("duplicate Codex binding")
            routes.add(route); self.bindings.append(binding)

    def connect(self,client):
        self.client=client; self.seen.clear()
        for thread in dict.fromkeys(x["thread"] for x in self.bindings):
            # Subscribe before reconciling; notifications during these reads
            # remain queued in the transport helper. No configuration changes.
            client.request("thread/resume",{"threadId":thread})
            self.reconcile(thread)

    def reconcile(self,thread):
        result=self.client.request("thread/read",{"threadId":thread,"includeTurns":True})
        transcript=result.get("thread") or {}
        if transcript.get("id")!=thread: raise ValueError("uncorrelated Codex transcript")
        targets={x["turn"] for x in self.bindings if x["thread"]==thread}
        for turn in transcript.get("turns") or []:
            if turn.get("id") in targets and turn.get("status") in self.TERMINAL:
                if turn.get("itemsView","full")!="full":
                    self.pending.add((thread,turn["id"]))
                    raise TranscriptPending("Codex completion history is not fully loaded")
                self.publish(thread,turn)
                self.pending.discard((thread,turn["id"]))
        if any(owner==thread for owner,turn in self.pending):
            # Keep the known wake across reconnects. Returning to an idle
            # event wait here would lose it if history lags the notification.
            raise TranscriptPending("Codex completion is not yet in history")

    def publish(self,thread,turn):
        messages=[]
        for item in turn.get("items") or []:
            if item.get("type")=="agentMessage" and item.get("phase")!="commentary":
                messages.append({"id":text(item["id"]),"text":item.get("text") or "",
                                 "phase":item.get("phase")})
        value={"schema":"CodexTurnResult","version":1,"threadId":thread,
               "turnId":text(turn["id"]),"status":turn["status"],"messages":messages}
        if turn.get("error") is not None:
            error=turn["error"]
            value["error"]={key:error[key] for key in ("message","codexErrorInfo","additionalDetails") if key in error}
        body=json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(",",":"))
        for binding in self.bindings:
            if (binding["thread"],binding["turn"])!=(thread,turn["id"]): continue
            identity=json.dumps(["codex-turn",thread,turn["id"],binding["channel"],binding["sender"]],
                                ensure_ascii=False,separators=(",",":"))
            if identity in self.seen: continue
            self.mailbox.send(identity,binding["channel"],turn["id"],binding["sender"],body)
            # An uncertain send leaves this unset. Reconnect reads the ledger
            # by the exact application identity before any further decision.
            self.seen.add(identity)

    def accept(self,notification):
        if notification.get("method")!="turn/completed": return False
        params=notification.get("params") or {}; turn=params.get("turn") or {}
        thread=params.get("threadId")
        if turn.get("status") not in self.TERMINAL: return False
        matches=[x for x in self.bindings if (x["thread"],x["turn"])==(thread,turn.get("id"))]
        if not matches: return False
        self.pending.add((thread,turn["id"]))
        self.reconcile(thread)
        return True

    def next_event(self):
        if self.client.pending_notifications:
            return self.accept(self.client.pending_notifications.pop(0))
        connection=self.client.connection
        # Idle is an event wait, not a transcript read every socket timeout.
        # Requests retain the transport's ordinary deadline.
        peer=connection.socket; timeout=peer.gettimeout()
        try:
            peer.settimeout(None)
            event=connection.receive_json()
        finally:
            peer.settimeout(timeout)
        return self.accept(event)


def transport(path):
    """Load an operator-selected helper; no model can choose executable code."""
    spec=importlib.util.spec_from_file_location("configured_codex_transport",path,
        loader=SourceFileLoader("configured_codex_transport",str(path)))
    if spec is None or spec.loader is None: raise ValueError("Codex transport module unavailable")
    module=importlib.util.module_from_spec(spec); sys.modules[spec.name]=module
    spec.loader.exec_module(module)
    return module.AppServer


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",required=True)
    args=parser.parse_args(); config=json.loads(Path(args.config).read_text())
    box=Mailbox(config["socket"],config["space"],config["worker"],expected=config.get("storage_identity"))
    bridge=CodexBridge(box,config["bindings"])
    factory=transport(config["transport_module"]); delay=.1
    try:
        while True:
            client=None
            try:
                client=factory(config["app_server_socket"])
                box.reconnect(); bridge.connect(client); delay=.1
                while True: bridge.next_event()
            except (OSError,EOFError,Uncertain):
                time.sleep(delay); delay=min(5,delay*2)
            finally:
                if client is not None: client.close()
    except KeyboardInterrupt:
        pass
    finally:
        box.close()


if __name__=="__main__": main()

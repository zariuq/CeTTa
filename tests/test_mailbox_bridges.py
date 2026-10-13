"""Directory notifications and Codex completions against real native storage."""
import json
import os
from pathlib import Path
import queue
import socket
import sys
import struct
import subprocess
import time
import unittest
from unittest.mock import patch
from concurrent.futures import ThreadPoolExecutor

from test_native_mailbox import ROOT, SERVER, WORKER, Fixture
sys.path.insert(0,str(ROOT/"tools"))
from native_mailbox import Mailbox
from mailbox_bridge import DirectoryBridge, deliver
from mailbox_codex import CodexBridge, TranscriptPending, transport
from space_service_client import E, S, Var, Uncertain


class AppServerFixture:
    def __init__(self,turns):
        self.turns=turns; self.calls=[]; self.pending_notifications=[]
        self.connection=self; self.socket=socket.socket(); self.events=queue.Queue()

    def request(self,method,params):
        self.calls.append((method,params))
        if method=="thread/resume": return {"thread":{"id":params["threadId"]}}
        if method=="thread/read": return {"thread":{"id":params["threadId"],"turns":self.turns}}
        raise AssertionError("watcher attempted to start or steer model work")

    def receive_json(self): return self.events.get(timeout=3)
    def close(self): self.socket.close()


@unittest.skipUnless(SERVER and WORKER,"set CETTA_SPACE_SERVICE and CETTA_VALUE_WORKER")
class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.fixture=Fixture(); self.addCleanup(self.fixture.close)
        self.box=Mailbox(self.fixture.socket,"mailbox",WORKER); self.addCleanup(self.box.close)
        self.inbox=self.fixture.path/"inbox"

    def register(self,correlation="request",identity="wait",generation=1,channel="review"):
        self.box.register(identity,channel,correlation,"codex","agent",generation,E("MailboxResume",1,"review",identity))

    def folder(self):
        bridge=DirectoryBridge(self.box,[{"name":"review","sender":"codex","directory":str(self.inbox)}])
        self.addCleanup(bridge.close); return bridge

    def pump(self,bridge,predicate):
        deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            if predicate(): return
            bridge.poll(.05)
        self.assertTrue(predicate())

    def test_atomic_arrival_commits_before_ack_and_duplicate_survives_restart(self):
        bridge=self.folder(); self.register()
        source=deliver(self.inbox,"source-1","request","codex","answer")
        self.pump(bridge,lambda:not source.exists())
        result=self.box.resolve("wait"); self.assertEqual(result[6][-1],"answer")
        bridge.close(); self.fixture.restart(); self.box.reconnect()
        duplicate=deliver(self.inbox,"source-1","request","codex","answer")
        resumed=self.folder()
        self.assertFalse(duplicate.exists()); self.assertEqual(resumed.ingested,1)
        self.assertEqual(self.box.resolve("wait"),result)
        self.assertEqual(self.box.space.match_count(E("MailboxPending",1,Var(0),Var(1),Var(2),Var(3))),0)

    def test_create_does_not_ack_a_still_open_copy(self):
        bridge=self.folder(); self.register()
        path=self.inbox/"copy.json"
        with path.open("w") as writer:
            json.dump({"id":"copy","correlation":"request","sender":"codex","body":"answer"},writer)
            writer.flush()
            bridge.poll(.05)
            self.assertTrue(path.exists()); self.assertEqual(bridge.ingested,0)
        self.pump(bridge,lambda:not path.exists())
        self.assertEqual(self.box.resolve("wait")[6][-1],"answer")

    def test_bad_occurrence_is_held_while_another_channel_progresses(self):
        bridge=self.folder(); self.register()
        malformed=self.inbox/"broken.json"; malformed.write_text("{")
        wrong=deliver(self.inbox,"wrong","request","impostor","wrong")
        good=deliver(self.inbox,"good","request","codex","good")
        self.pump(bridge,lambda:not good.exists())
        self.assertFalse(malformed.exists()); self.assertFalse(wrong.exists())
        self.assertEqual(len(list((self.inbox/"rejected").glob("*.json"))),2)
        self.assertEqual(self.box.resolve("wait")[6][-1],"good")

    def test_structured_headers_survive_restart_and_duplicate_delivery(self):
        bridge=self.folder();self.register()
        headers={"from":"lila","return_address":"lila","in_reply_to":"question"}
        source=deliver(self.inbox,"structured","request","codex","literal body",headers=headers)
        self.pump(bridge,lambda:not source.exists())
        identity=self.box.resolve("wait")[6][2]
        self.assertEqual(self.box.headers(identity),headers)
        self.fixture.restart();self.box.reconnect()
        duplicate=deliver(self.inbox,"structured","request","codex","literal body",headers=headers)
        bridge.rescan();self.assertFalse(duplicate.exists())
        self.assertEqual(self.box.headers(identity),headers)
        self.assertEqual(self.box.space.match_count(E("MailboxHeaders",1,Var(0),Var(1))),1)

    def test_invalid_json_and_plain_text_drops_get_reason_and_valid_example(self):
        bridge=self.folder()
        for name,body in (("missing-fields.json",'{"body":"hello"}'),("note.txt","hello")):
            (self.inbox/name).write_text(body)
        bridge.scan()
        self.assertFalse((self.inbox/"note.txt").exists())
        reasons=[json.loads(p.read_text()) for p in (self.inbox/"rejected").glob("*.reason")]
        self.assertEqual({r["source"] for r in reasons},{"missing-fields.json","note.txt"})
        self.assertTrue(all(r["reason"] and r["usage"]=="mailbox-post NAME --body-file FILE" for r in reasons))
        self.assertEqual(set(reasons[0]["example"]),{"id","correlation","sender","body"})

    def test_conflicting_headers_do_not_rewrite_an_existing_message(self):
        bridge=self.folder();self.register()
        headers={"from":"lila","return_address":"lila","in_reply_to":None}
        source=deliver(self.inbox,"one","request","codex","body",headers=headers)
        self.pump(bridge,lambda:not source.exists())
        identity=self.box.resolve("wait")[6][2]
        changed=deliver(self.inbox,"one","request","codex","body",headers=dict(headers,**{"from":"other"}))
        bridge.scan();self.assertFalse(changed.exists())
        self.assertEqual(self.box.headers(identity),headers)

    def test_deep_json_is_quarantined_and_cannot_crash_loop_on_rescan(self):
        self.inbox.mkdir()
        malformed=self.inbox/"deep.json"
        malformed.write_text("["*10000+"0"+"]"*10000)
        bridge=self.folder(); self.register()
        self.assertFalse(malformed.exists())
        self.assertEqual(len(list((self.inbox/"rejected").glob("*.json"))),1)
        self.assertFalse(bridge.retry)
        good=deliver(self.inbox,"good","request","codex","good")
        self.pump(bridge,lambda:not good.exists())
        bridge.rescan()
        self.assertEqual(self.box.resolve("wait")[6][-1],"good")

    def test_symlink_refusal_is_permanent_and_keeps_target_untouched(self):
        bridge=self.folder(); self.register()
        target=self.fixture.path/"outside"
        target.write_text("private")
        staged=self.inbox/".link"; staged.symlink_to(target)
        source=self.inbox/"link.json"; staged.rename(source)
        self.pump(bridge,lambda:bridge.held>0)
        self.assertFalse(source.is_symlink())
        self.assertFalse(bridge.retry)
        links=list((self.inbox/"rejected").glob("*.json"))
        self.assertEqual(len(links),1); self.assertTrue(links[0].is_symlink())
        self.assertEqual(target.read_text(),"private")

    def test_killed_bridge_worker_is_replaced_without_another_file_event(self):
        bridge=self.folder(); self.register()
        old=self.box.worker.process
        old.kill(); old.wait(timeout=5)
        source=deliver(self.inbox,"after-kill","request","codex","recovered")
        self.pump(bridge,lambda:not source.exists())
        self.assertNotEqual(self.box.worker.process.pid,old.pid)
        self.assertEqual(self.box.resolve("wait")[6][-1],"recovered")

    def test_uncertain_arrival_is_not_acknowledged_and_rescan_deduplicates(self):
        bridge=self.folder(); self.register()
        original=self.box.send
        def lost_reply(*args):
            original(*args); raise Uncertain(E("lost-ack"))
        self.box.send=lost_reply
        source=deliver(self.inbox,"id","request","codex","answer")
        self.pump(bridge,lambda:bridge.held>0)
        self.assertTrue(source.exists())
        self.box.send=original; self.fixture.restart(); self.box.reconnect(); bridge.rescan()
        self.assertFalse(source.exists()); self.assertEqual(self.box.resolve("wait")[6][-1],"answer")
        self.assertEqual(self.box.space.match_count(E("MailboxMessage",1,Var(0),Var(1),Var(2),Var(3),Var(4))),1)

    def test_folder_replacement_rearms_and_reconciles_complete_files(self):
        bridge=self.folder(); self.register()
        self.inbox.rename(self.fixture.path/"old")
        source=deliver(self.inbox,"fresh","request","codex","replacement")
        self.pump(bridge,lambda:not source.exists())
        self.assertEqual(self.box.resolve("wait")[6][-1],"replacement")

    def test_transient_owner_outage_recovers_without_another_file_event(self):
        bridge=self.folder(); self.register(); self.fixture.stop(crash=True)
        source=deliver(self.inbox,"outage","request","codex","retained")
        self.pump(bridge,lambda:bridge.held>0)
        self.assertTrue(source.exists())
        self.fixture.start()
        self.pump(bridge,lambda:not source.exists())
        self.assertEqual(self.box.resolve("wait")[6][-1],"retained")

    def test_overflow_event_rescans_published_files(self):
        bridge=self.folder(); self.register()
        source=deliver(self.inbox,"overflow","request","codex","rescanned")
        packet=struct.pack("iIII",-1,bridge.OVERFLOW,0,0)
        original_read=os.read; original_select=__import__("select").select; packets=[packet]
        def read(fd,size):
            if fd!=bridge.fd: return original_read(fd,size)
            if packets: return packets.pop()
            raise BlockingIOError()
        def ready(readers,writers,errors,timeout=None):
            if readers==[bridge.fd] and not writers: return (readers,[],[])
            return original_select(readers,writers,errors,timeout)
        with patch("mailbox_bridge.select.select",side_effect=ready), \
             patch("mailbox_bridge.os.read",side_effect=read):
            bridge.poll()
        self.assertFalse(source.exists()); self.assertEqual(bridge.overflows,1)
        self.assertEqual(self.box.resolve("wait")[6][-1],"rescanned")

    def test_replacing_folders_retires_old_watches(self):
        bridge=self.folder()
        for number in range(4):
            self.inbox.rename(self.fixture.path/("old-"+str(number)))
            self.inbox.mkdir(); bridge.rescan()
            self.assertEqual(set(bridge.watches.values()),{self.inbox,self.inbox.parent})
            self.assertEqual(len(bridge.watches),2)

    def test_same_source_identity_on_two_bindings_stays_distinct(self):
        other=self.fixture.path/"other"
        bridge=DirectoryBridge(self.box,[{"name":"review","sender":"codex","directory":str(self.inbox)},
                                       {"name":"other","sender":"codex","directory":str(other)}])
        self.addCleanup(bridge.close)
        deliver(self.inbox,"same","request","codex","one")
        deliver(other,"same","request","codex","two")
        self.pump(bridge,lambda:bridge.ingested==2)
        self.assertEqual(self.box.space.match_count(E("MailboxMessage",1,Var(0),Var(1),Var(2),Var(3),Var(4))),2)

    def test_post_tool_publishes_text_with_a_stable_retry_identity_and_rejects_oversize(self):
        bridge=self.folder(); self.register()
        config=self.fixture.path/"bindings.json"
        config.write_text(json.dumps({"bindings":[{"name":"review","sender":"codex","directory":str(self.inbox)}]}))
        command=[sys.executable,str(ROOT/"tools/mailbox_post.py"),"--config",str(config),
                 "--id","note","--correlation","request","review"]
        for _ in range(2):
            result=subprocess.run(command,input="a plain note 🙂",text=True,capture_output=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            self.pump(bridge,lambda:not Path(json.loads(result.stdout)["path"]).exists())
        self.assertEqual(self.box.resolve("wait")[6][-1],"a plain note 🙂")
        self.assertEqual(self.box.space.match_count(E("MailboxMessage",1,Var(0),Var(1),Var(2),Var(3),Var(4))),1)
        config.write_text(json.dumps({"max_file_bytes":1,"bindings":[{"name":"review","sender":"codex","directory":str(self.inbox)}]}))
        result=subprocess.run(command,input="too large",text=True,capture_output=True,timeout=5)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(list(self.inbox.glob("*.json")),[])

    def test_storage_backpressure_keeps_source_and_retries_without_another_file_event(self):
        self.fixture.options={"max_facts":8}; self.fixture.restart(); self.box.reconnect()
        bridge=self.folder(); self.register()
        fillers=[E("Filler",i) for i in range(4)]
        self.box.space.batch(*(E("add",value) for value in fillers))
        source=deliver(self.inbox,"held","request","codex","retained")
        self.pump(bridge,lambda:bridge.held>0)
        self.assertTrue(source.exists()); self.assertIsNone(self.fixture.process.poll())
        self.box.space.batch(*(E("remove",value) for value in fillers))
        self.pump(bridge,lambda:not source.exists())
        self.assertEqual(self.box.resolve("wait")[6][-1],"retained")

    def test_post_tool_retains_named_return_route_and_refuses_unknown_reply_address(self):
        config=self.fixture.path/"replyable.json"
        config.write_text(json.dumps({"bindings":[
            {"name":"review","sender":"codex","directory":str(self.inbox)},
            {"name":"writer","sender":"local","directory":str(self.fixture.path/"writer")}
        ]}))
        command=[sys.executable,str(ROOT/"tools/mailbox_post.py"),"--config",str(config),
                 "--from","writer","--reply-to","writer","review"]
        result=subprocess.run(command,input="please review",text=True,capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)
        receipt=json.loads(result.stdout)
        envelope=json.loads(Path(receipt["path"]).read_text())
        self.assertEqual(receipt["return_address"],"writer")
        self.assertEqual(envelope["headers"],{"from":"writer","return_address":"writer","in_reply_to":None})
        before=set(self.inbox.glob("*.json"))
        command[command.index("--reply-to")+1]="missing"
        refused=subprocess.run(command,input="bad address",text=True,capture_output=True,timeout=5)
        self.assertNotEqual(refused.returncode,0)
        self.assertIn("reply address",refused.stderr)
        self.assertEqual(set(self.inbox.glob("*.json")),before)

    def codex(self,turns):
        client=AppServerFixture(turns); self.addCleanup(client.close)
        bridge=CodexBridge(self.box,[{"thread":"thread","turn":"turn","channel":"review","sender":"codex"}])
        bridge.connect(client); return bridge,client

    def completed(self,status="completed"):
        return {"id":"turn","status":status,"items":[{"id":"progress","type":"agentMessage","phase":"commentary","text":"working"},
                 {"id":"final","type":"agentMessage","phase":"final_answer","text":"done"}]}

    def test_codex_notification_path_has_no_idle_transcript_poll(self):
        self.register("turn"); bridge,client=self.codex([{"id":"turn","status":"inProgress"}])
        self.assertEqual([x[0] for x in client.calls],["thread/resume","thread/read"])
        before=dict(self.box.metrics)
        time.sleep(.03); self.assertEqual(self.box.metrics,before)
        client.turns=[self.completed()]
        client.events.put({"method":"turn/completed","params":{"threadId":"thread","turn":self.completed()}})
        self.assertTrue(bridge.next_event())
        body=json.loads(self.box.resolve("wait")[6][-1])
        self.assertEqual(body["status"],"completed"); self.assertEqual(len(body["messages"]),1)
        self.assertEqual(body["messages"][0]["text"],"done")
        self.assertNotIn("turn/start",[x[0] for x in client.calls])

    def test_codex_reconnect_reconciles_missed_completion_without_reopening_consumed_message(self):
        self.register("turn"); bridge,client=self.codex([self.completed()])
        result=self.box.resolve("wait"); self.box.settle("wait","agent",1)
        self.fixture.restart(); self.box.reconnect(); bridge.connect(client)
        self.assertEqual(self.box.resolve("wait")[0],S("MailboxSettled"))
        self.assertEqual(self.box.space.match_count(E("MailboxPending",1,Var(0),Var(1),Var(2),Var(3))),0)
        self.assertEqual(result[6][-1],json.dumps(json.loads(result[6][-1]),ensure_ascii=False,sort_keys=True,separators=(",",":")))

    def test_codex_wrong_turn_and_wrong_thread_cannot_wake(self):
        self.register("turn"); bridge,client=self.codex([])
        for thread,turn in [("wrong",self.completed()),("thread",{"id":"wrong","status":"completed"})]:
            self.assertFalse(bridge.accept({"method":"turn/completed","params":{"threadId":thread,"turn":turn}}))
        self.assertEqual(self.box.resolve("wait")[0],S("MailboxWaiting"))

    def test_codex_failure_is_explicit_data_and_not_a_success_reply(self):
        self.register("turn"); turn=self.completed("failed"); turn["error"]={"message":"provider unavailable"}
        self.codex([turn]); result=json.loads(self.box.resolve("wait")[6][-1])
        self.assertEqual(result["status"],"failed"); self.assertEqual(result["error"]["message"],"provider unavailable")

    def test_codex_known_completion_survives_a_lagging_transcript(self):
        self.register("turn"); bridge,client=self.codex([{"id":"turn","status":"inProgress"}])
        notification={"method":"turn/completed","params":{"threadId":"thread","turn":self.completed()}}
        with self.assertRaises(TranscriptPending): bridge.accept(notification)
        with self.assertRaises(TranscriptPending): bridge.connect(client)
        self.assertEqual(self.box.resolve("wait")[0],S("MailboxWaiting"))
        client.turns=[self.completed()]; bridge.connect(client)
        self.assertEqual(bridge.pending,set())
        self.assertEqual(json.loads(self.box.resolve("wait")[6][-1])["messages"][0]["text"],"done")

    def test_codex_summary_cannot_be_acknowledged_as_full_result(self):
        self.register("turn"); bridge,client=self.codex([])
        summary=self.completed(); summary["itemsView"]="summary"; client.turns=[summary]
        with self.assertRaises(TranscriptPending): bridge.reconcile("thread")
        self.assertEqual(self.box.resolve("wait")[0],S("MailboxWaiting"))
        client.turns=[self.completed()]; bridge.connect(client)
        self.assertEqual(bridge.pending,set())
        self.assertEqual(self.box.resolve("wait")[0],S("MailboxResult"))

    @unittest.skipUnless(os.environ.get("CETTA_CODEX_TRANSPORT"),"set installed AppServer helper for wire fixture")
    def test_installed_codex_transport_delivers_completion_while_other_work_runs(self):
        from support.codex_app_server_fixture import WireAppServer
        self.register("turn")
        fixture=WireAppServer(self.fixture.path/"codex.sock")
        self.addCleanup(fixture.close)
        client=transport(os.environ["CETTA_CODEX_TRANSPORT"])(str(fixture.path))
        self.addCleanup(client.close)
        bridge=CodexBridge(self.box,[{"thread":"thread","turn":"turn","channel":"review","sender":"codex"}])
        bridge.connect(client)
        previous=list(fixture.calls)
        other=self.box.client.open("other-activities")
        with ThreadPoolExecutor() as executor:
            pending=executor.submit(bridge.next_event)
            for number in range(4):
                other.add(E("PeriodicDuty",number)); time.sleep(.02)
                self.assertFalse(pending.done())
            self.assertEqual(fixture.calls,previous)
            started=time.monotonic(); fixture.complete(self.completed())
            self.assertTrue(pending.result(timeout=3))
            result=self.box.resolve("wait")
            elapsed=(time.monotonic()-started)*1000
        self.assertEqual(json.loads(result[6][-1])["messages"][0]["text"],"done")
        self.assertEqual(other.match_count(E("PeriodicDuty",Var(0))),4)
        self.assertTrue(all(method in ("initialize","initialized","thread/resume","thread/read")
                            for method,params in fixture.calls))
        print("CODEX_MAILBOX_WIRE "+json.dumps({"arrival_to_ready_ms":round(elapsed,3),
                    "periodic_duties":4,"idle_rpc_polls":0,"model_calls":0}))


if __name__=="__main__": unittest.main(verbosity=2)

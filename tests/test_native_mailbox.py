"""Real native storage, MeTTa receives and revision notifications, without SQL."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import socket
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from space_service_client import Client, E, S, Var, ServiceError, Uncertain
from native_mailbox import Mailbox, MailboxError

SERVER=os.environ.get("CETTA_SPACE_SERVICE")
WORKER=os.environ.get("CETTA_VALUE_WORKER")


class Fixture:
    def __init__(self, **options):
        self.temp=tempfile.TemporaryDirectory(prefix="native-mailbox-")
        self.path=Path(self.temp.name); self.socket=str(self.path/"space.sock")
        self.options=options; self.process=None; self.start()

    def start(self):
        self.log=(self.path/"stderr").open("ab")
        args=[SERVER,"--socket",self.socket,"--data",str(self.path/"store")]
        for key,value in self.options.items():
            args.extend(["--"+key.replace("_","-"),str(value)])
        self.process=subprocess.Popen(args,cwd=ROOT,stderr=self.log,stdout=subprocess.DEVNULL)
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if self.process.poll() is not None:
                raise AssertionError((self.path/"stderr").read_text())
            try:
                with Client(self.socket) as client:
                    client.hello(); return
            except (OSError,Uncertain):
                time.sleep(.01)
        raise AssertionError("native mailbox owner did not start")

    def stop(self, crash=False):
        if self.process and self.process.poll() is None:
            self.process.kill() if crash else self.process.terminate()
            self.process.wait(timeout=10)
        self.log.close()

    def restart(self):
        self.stop(crash=True); self.start()

    def close(self):
        self.stop(); self.temp.cleanup()


@unittest.skipUnless(SERVER and WORKER,"set CETTA_SPACE_SERVICE and CETTA_VALUE_WORKER")
class NativeMailboxTests(unittest.TestCase):
    def setUp(self):
        self.fixture=Fixture(); self.addCleanup(self.fixture.close)

    def mailbox(self):
        result=Mailbox(self.fixture.socket,"mailbox",WORKER)
        self.addCleanup(result.close); return result

    def register(self, box, identity="wait", correlation="request", generation=1):
        return box.register(identity,"codex-review",correlation,"codex","godel",generation,
                            E("MailboxResume",1,"review-result","quoted continuation"))

    def test_watch_registration_closes_read_write_race(self):
        with Client(self.fixture.socket) as client:
            space=client.open("notifications")
            rev=space.revision
            space.add(E("Arrival","before subscription"))
            started=time.monotonic()
            answer=space.watch(rev,1000)
            self.assertEqual(answer[0],S("changed"))
            self.assertLess(time.monotonic()-started,.5)

    def test_watch_leaves_other_spaces_and_requests_free(self):
        with Client(self.fixture.socket,timeout=3) as receiver, Client(self.fixture.socket) as sender:
            observed=receiver.open("observed"); source=sender.open("observed")
            unrelated=sender.open("unrelated")
            with ThreadPoolExecutor() as executor:
                waiting=executor.submit(observed.watch,observed.revision,2000)
                unrelated.add(E("Unrelated","work"))
                sender.hello()
                self.assertFalse(waiting.done())
                source.add(E("Arrival","wake"))
                self.assertEqual(waiting.result(timeout=2)[0],S("changed"))
            now=time.monotonic()
            self.assertEqual(observed.watch(source.revision,30)[0],S("unchanged"))
            self.assertGreaterEqual(time.monotonic()-now,.02)

    def test_first_occurrence_is_explicit_and_preserves_multiplicity(self):
        with Client(self.fixture.socket) as client:
            space=client.open("ordered")
            space.batch(E("add",E("Message","first")),E("add",E("Message","second")))
            pattern=E("Message",Var(0)); view=space.query_first(pattern,pattern)
            self.assertEqual(view[0],S("selection"))
            self.assertEqual(space.fetch(view)[3],E("rows",E("Message","first")))
            space.close_query(view)
            self.assertEqual(space.match_count(pattern),2)

    def test_message_before_receive_and_duplicates_after_consumption(self):
        box=self.mailbox()
        self.assertEqual(box.send("m1","codex-review","request","codex","answer"),"accepted")
        self.register(box)
        result=box.resolve("wait")
        self.assertEqual(result[0],S("MailboxResult")); self.assertEqual(result[6][-1],"answer")
        self.assertEqual(box.settle("wait","godel",1),"settled")
        self.assertEqual(box.send("m1","codex-review","request","codex","answer"),"duplicate")
        self.assertEqual(box.resolve("wait")[0],S("MailboxSettled"))
        with self.assertRaises(MailboxError):
            box.send("m1","codex-review","request","codex","changed")

    def test_opaque_body_preserves_control_characters_and_large_text(self):
        box=self.mailbox(); self.register(box)
        body='\0\r\n(shell "untrusted") ♥ '+"x"*65536
        box.send("opaque","codex-review","request","codex",body)
        self.assertEqual(box.resolve("wait")[6][-1],body)
        self.assertEqual(box.send("opaque","codex-review","request","codex",body),"duplicate")

    def test_waiting_receive_wakes_without_model_polling(self):
        receiver=self.mailbox(); sender=self.mailbox(); self.register(receiver)
        with ThreadPoolExecutor() as executor:
            future=executor.submit(receiver.await_result,"wait",2000)
            time.sleep(.08)
            before=dict(receiver.metrics)
            time.sleep(.08)
            self.assertEqual(receiver.metrics,before)
            started=time.monotonic()
            sender.send("m1","codex-review","request","codex","arrived")
            result=future.result(timeout=3)
            elapsed=(time.monotonic()-started)*1000
            self.assertEqual(result[6][-1],"arrived")
            self.assertEqual(receiver.metrics["notifications"],1)
            print("MAILBOX_WAKE "+json.dumps({"arrival_to_ready_ms":round(elapsed,3),"notifications":1,"model_calls":0}))

    def test_wrong_channel_correlation_and_sender_cannot_resume_wait(self):
        box=self.mailbox(); self.register(box)
        for i,(channel,correlation,sender) in enumerate([
            ("elsewhere","request","codex"),("codex-review","old-request","codex"),
            ("codex-review","request","other")]):
            box.send(f"wrong{i}",channel,correlation,sender,"unrelated")
        self.assertEqual(box.resolve("wait")[0],S("MailboxWaiting"))
        box.send("correct","codex-review","request","codex","right")
        self.assertEqual(box.resolve("wait")[6][-1],"right")

    def test_cancelled_receive_keeps_late_message_inert(self):
        box=self.mailbox(); self.register(box)
        with self.assertRaises(MailboxError): box.cancel("wait","godel",2)
        self.assertEqual(box.cancel("wait","godel",1),"cancelled")
        marker=str(self.fixture.path/"must-not-exist")
        body=f'(shell "touch {marker}")'
        box.send("late","codex-review","request","codex",body)
        self.assertEqual(box.resolve("wait")[0],S("MailboxCancelled"))
        self.assertFalse(Path(marker).exists())
        self.assertEqual(box.space.match_count(E("MailboxLate",1,"late","wait","godel",1)),1)
        with self.assertRaises(MailboxError): self.register(box,"reuse",generation=2)
        self.register(box,"fresh","fresh-request",2)
        self.assertEqual(box.resolve("fresh")[0],S("MailboxWaiting"))

    def test_activity_generation_validation_precedes_cancel_and_settle(self):
        box=self.mailbox(); self.register(box)
        for invalid in (True,False,0,-1,1.0,"1"):
            with self.assertRaises(ValueError): box.cancel("wait","godel",invalid)
            with self.assertRaises(ValueError): box.settle("wait","godel",invalid)
        self.assertEqual(box.resolve("wait")[0],S("MailboxWaiting"))

    def test_recreated_consumer_observes_ready_without_consuming_twice(self):
        box=self.mailbox(); self.register(box)
        box.send("m1","codex-review","request","codex","answer")
        result=box.resolve("wait"); box.close()
        recovered=self.mailbox()
        self.assertEqual(self.register(recovered),"duplicate")
        self.assertEqual(recovered.resolve("wait"),result)
        self.assertEqual(recovered.space.match_count(E("MailboxReady",1,Var(0),Var(1),Var(2),Var(3),Var(4))),1)
        recovered.settle("wait","godel",1)
        self.assertEqual(self.register(recovered),"duplicate")
        self.assertEqual(recovered.resolve("wait")[0],S("MailboxSettled"))

    def test_cli_waiter_recovers_after_process_death(self):
        box=self.mailbox()
        args=[sys.executable,str(ROOT/"tools/mailbox.py"),"--socket",self.fixture.socket,
              "--space","mailbox","--worker",WORKER]
        wait=args+["wait","cli-wait","--channel","review","--correlation","request",
                   "--sender","codex","--owner","cli-activity","--generation","1"]
        def start():
            child=subprocess.Popen(wait,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            def stop():
                if child.poll() is None: child.kill()
                child.communicate(timeout=5)
            self.addCleanup(stop); return child
        first=start(); deadline=time.monotonic()+3
        pattern=E("MailboxRegistration",1,"cli-wait",Var(0))
        while box.space.match_count(pattern)!=1 and time.monotonic()<deadline:
            if first.poll() is not None: self.fail(first.communicate()[1])
            time.sleep(.01)
        self.assertEqual(box.space.match_count(pattern),1)
        first.kill(); first.communicate(timeout=3)
        box.send("cli-message","review","request","codex","survives consumer crash")
        recovered=start(); output,error=recovered.communicate(timeout=5)
        self.assertEqual(recovered.returncode,0,error)
        self.assertEqual(json.loads(output)[-1][-1],"survives consumer crash")
        self.assertEqual(box.space.match_count(pattern),1)
        self.assertEqual(box.resolve("cli-wait")[0],S("MailboxResult"))
        settled=subprocess.run(args+["settle","cli-wait","--owner","cli-activity"],
                    cwd=ROOT,capture_output=True,text=True,timeout=5)
        self.assertEqual(settled.returncode,0,settled.stderr)
        self.assertEqual(box.resolve("cli-wait")[0],S("MailboxSettled"))

    def test_owner_restart_recovers_receive_message_and_continuation(self):
        box=self.mailbox(); self.register(box)
        box.send("m1","codex-review","request","codex","before restart")
        self.fixture.restart(); box.client.close()
        resumed=self.mailbox()
        self.assertEqual(self.register(resumed),"duplicate")
        result=resumed.resolve("wait")
        self.assertEqual(result[6][-1],"before restart")
        self.fixture.restart(); resumed.client.close()
        reopened=self.mailbox()
        self.assertEqual(reopened.resolve("wait"),result)
        self.assertEqual(reopened.space.match_count(E("MailboxReady",1,"wait",Var(0),Var(1),Var(2),Var(3))),1)

    def test_lost_commit_reply_is_recovered_without_repeating_receive(self):
        box=self.mailbox(); self.register(box)
        box.send("m1","codex-review","request","codex","answer")
        batch=box.space.batch
        def lose_reply(*ops,**kwargs):
            batch(*ops,**kwargs)
            raise Uncertain(E("fixture-lost-reply"))
        box.space.batch=lose_reply
        with self.assertRaises(Uncertain): box.resolve("wait")
        self.fixture.restart()
        resumed=self.mailbox()
        self.assertEqual(resumed.resolve("wait")[6][-1],"answer")
        self.assertEqual(resumed.space.match_count(E("MailboxReady",1,"wait",Var(0),Var(1),Var(2),Var(3))),1)
        self.assertEqual(resumed.space.match_count(E("MailboxPending",1,Var(0),Var(1),Var(2),"m1")),0)

    def test_competing_receivers_commit_only_one_consumption(self):
        left=self.mailbox(); right=self.mailbox(); self.register(left)
        left.send("m1","codex-review","request","codex","answer")
        with ThreadPoolExecutor() as executor:
            a=executor.submit(left.resolve,"wait"); b=executor.submit(right.resolve,"wait")
            self.assertEqual(a.result(timeout=5),b.result(timeout=5))
        self.assertEqual(left.space.match_count(E("MailboxReady",1,"wait",Var(0),Var(1),Var(2),Var(3))),1)

    def test_completion_survives_native_checkpoint(self):
        box=self.mailbox(); self.register(box)
        box.send("m1","codex-review","request","codex","answer")
        result=box.resolve("wait")
        box.client.call("compact",box.space.uuid)
        self.fixture.restart()
        resumed=self.mailbox()
        self.assertEqual(resumed.resolve("wait"),result)


    def test_waiter_reconnects_while_owner_restarts_and_other_work_continues(self):
        receiver=self.mailbox(); self.register(receiver)
        with ThreadPoolExecutor() as executor:
            future=executor.submit(receiver.await_result,"wait",2000)
            time.sleep(.03); self.fixture.restart()
            sender=self.mailbox()
            sender.client.open("other-work").add(E("Work","continues"))
            sender.send("after","codex-review","request","codex","after restart")
            self.assertEqual(future.result(timeout=3)[6][-1],"after restart")

    def test_watch_budget_reserves_ordinary_requests_and_releases_on_disconnect(self):
        self.fixture.options["max_watchers"]=1; self.fixture.restart()
        with Client(self.fixture.socket,timeout=3) as receiver, Client(self.fixture.socket) as other:
            space=receiver.open("limited"); writer=other.open("limited")
            with ThreadPoolExecutor() as executor:
                pending=executor.submit(space.watch,space.revision,2000)
                time.sleep(.03)
                with self.assertRaises(ServiceError) as refused: writer.watch(writer.revision,100)
                self.assertEqual(refused.exception.reason,S("watch-capacity"))
                other.hello(); writer.add(E("Still","responsive"))
                self.assertEqual(pending.result(timeout=2)[0],S("changed"))
                future=executor.submit(space.watch,writer.revision,2000)
                time.sleep(.03); receiver.connection.shutdown(socket.SHUT_RDWR)
                with self.assertRaises(Uncertain): future.result(timeout=2)
                self.assertEqual(writer.watch(writer.revision,20)[0],S("unchanged"))

    def test_future_revision_is_refused_and_drop_wakes_registered_watch(self):
        with Client(self.fixture.socket,timeout=3) as receiver, Client(self.fixture.socket) as owner:
            space=receiver.open("droppable")
            with self.assertRaises(ServiceError) as refused: space.watch(space.revision+1,100)
            self.assertEqual(refused.exception.reason,S("invalid-watch"))
            with ThreadPoolExecutor() as executor:
                future=executor.submit(space.watch,space.revision,2000)
                time.sleep(.03)
                owner.call("drop",space.uuid,space.id,space.revision,owner.hello()[4])
                with self.assertRaises(ServiceError) as gone: future.result(timeout=2)
                self.assertEqual(gone.exception.reason,S("missing-space"))

    def test_waiter_does_not_create_a_replacement_for_deleted_storage(self):
        box=self.mailbox(); self.register(box)
        with Client(self.fixture.socket) as client:
            client.call("drop",box.space.uuid,box.space.id,box.space.revision,client.hello()[4])
        with self.assertRaises(ServiceError): box.reconnect()
        self.assertEqual(box.client.list_spaces(),[])

    def test_native_wire_records_use_indexed_string_identity_candidates(self):
        box=self.mailbox()
        box.space.batch(*(E("add",E("MailboxMessage",1,f"old-{i}","review","old","codex","body")) for i in range(1000)))
        box.send("target","review","request","codex","body")
        pattern=E("MailboxMessage",1,"target",Var(0),Var(1),Var(2),Var(3))
        for _ in range(6):
            profile=box.space.profile_query(pattern,pattern,2)
            self.assertEqual(box.space.fetch(profile[1])[3][1][2],"target")
            box.space.close_query(profile[1])
        self.assertLessEqual(profile[2][1],2)


if __name__=="__main__": unittest.main(verbosity=2)

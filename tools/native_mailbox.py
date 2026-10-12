"""Native named mailboxes: MeTTa policy over conditional named-space batches.

No SQLite, model call, generated code or per-mailbox thread. Message bodies
and continuations are inert data. The caller owns external action receipts.
"""
from __future__ import annotations
from pathlib import Path
import time
import uuid

from space_service_client import Client, Space, E, S, Var, ServiceError, Uncertain
from value_worker_client import Worker


class MailboxError(RuntimeError):
    pass


class Changed(RuntimeError):
    pass


def text(value):
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError("nonempty mailbox text required")
    return value


def activity_generation(value):
    if not isinstance(value,int) or isinstance(value,bool) or value<1:
        raise ValueError("positive activity generation required")
    return value


class Mailbox:
    def __init__(self, socket, name, worker, *, timeout=30, attempts=8, expected=None):
        self.client = Client(str(socket), timeout=timeout)
        self.worker = Worker(worker, Path(__file__).resolve().parents[1] /
                             "lib/petta/lib_mailbox.metta", "mailbox:dispatch", timeout=timeout)
        self.attempts = attempts
        if not isinstance(attempts, int) or isinstance(attempts,bool) or attempts < 1:
            self.close(); raise ValueError("positive contention budget required")
        try:
            hello=self.client.hello()
            capabilities=hello[5][1:]
            if S("first-occurrence") not in capabilities or S("revision-waits") not in capabilities:
                raise MailboxError("native mailbox service capabilities unavailable")
            if expected is None:
                self.space = self.client.open(text(name), "native")
            else:
                if hello[2]!=expected[0]:
                    raise MailboxError("mailbox storage identity changed")
                # Recovery may describe an existing space, never create a
                # replacement with the old name and newly empty authority.
                self.space = Space(self.client,self.client.call("describe",*expected))
                if self.space.name!=text(name):
                    raise MailboxError("mailbox space identity changed")
            if self.space.backend!="native":
                raise MailboxError("native mailbox backend required")
        except BaseException:
            self.close(); raise
        self.metrics = {"reads": 0, "plans": 0, "commits": 0, "notifications": 0}

    def close(self):
        self.client.close()
        self.worker.close()

    def reconnect(self):
        """Reopen only the same durable owner, never create a replacement space."""
        self.client.close()
        hello=self.client.hello()
        if hello[2]!=self.space.uuid:
            raise MailboxError("mailbox storage identity changed")
        self.space.describe()

    def _rows(self, pattern, revision, first=False):
        self.metrics["reads"] += 1
        view = self.space.query_first(pattern, pattern) if first else self.space.query(pattern, pattern, 2)
        try:
            if view[4] != revision:
                raise Changed()
            page = self.space.fetch(view)
            if not page[5]:
                raise MailboxError("nonunique mailbox record")
            return tuple(page[3][1:])
        finally:
            try:
                self.space.close_query(view)
            except (ServiceError, Uncertain, OSError):
                pass  # cleanup cannot undo the captured read or a durable write

    def _one(self, rows):
        if len(rows) > 1:
            raise MailboxError("nonunique mailbox identity")
        return rows[0] if rows else None

    def _plan(self, request):
        self.metrics["plans"] += 1
        value = self.worker.call(request)
        if not isinstance(value, tuple) or not value:
            raise MailboxError("invalid mailbox policy reply")
        if value[0] == S("MailboxFailure"):
            raise MailboxError(str(value[1]))
        if len(value) != 3 or value[0] != S("MailboxPlan"):
            raise MailboxError("invalid mailbox plan")
        if value[2]:
            # An uncertain mutation is surfaced; it is never resubmitted
            # against a fresh revision here. Application identities survive
            # the bounded transport receipt cache and allow later inspection.
            self.space.batch(*value[2], request_id=uuid.uuid4().hex)
            self.metrics["commits"] += 1
        return value[1].name

    def _operation(self, function):
        for _ in range(self.attempts):
            try:
                self.space.describe()
                return function(self.space.revision)
            except Changed:
                pass
            except ServiceError as error:
                if error.reason != S("stale-revision"):
                    raise
            time.sleep(.001)
        raise MailboxError("mailbox contention; no decision committed")

    def send(self, identity, channel, correlation, sender, body):
        identity, channel, correlation, sender = map(text, (identity, channel, correlation, sender))
        if not isinstance(body, str):
            raise ValueError("mailbox body must be text data")
        message=E("MailboxMessage",1,identity,channel,correlation,sender,body)
        result=self._operation(lambda rev: self._plan(E("send",message,
            self._rows(E("MailboxMessage",1,identity,Var(0),Var(1),Var(2),Var(3)),rev))))
        # The durable message is acknowledged even if a disposable policy
        # worker later fails to match it. A recovered receiver can finish.
        return result

    def records(self, pattern, revision, limit):
        """A complete indexed view at one revision; overflow is a refusal.

        The native owner rejects a view exceeding its row/byte budget rather
        than returning a prefix that could hide an interactive arrival.
        """
        view = self.space.query(pattern, pattern, limit)
        try:
            if view[4] != revision:
                raise Changed()
            rows, offset = [], 0
            while True:
                page = self.space.fetch(view, offset)
                rows.extend(page[3][1:])
                if page[5]:
                    return tuple(rows)
                offset = page[4]
        finally:
            try: self.space.close_query(view)
            except (ServiceError, Uncertain, OSError): pass

    def _active(self, tag, revision, limit):
        waits = self.records(E("MailboxWait",1,Var(0),Var(1),Var(2),Var(3),Var(4),Var(5),
                               E("MailboxResume",1,tag,Var(6))),revision,limit)
        ready = self.records(E("MailboxReady",1,Var(0),Var(1),Var(2),
                               E("MailboxResume",1,tag,Var(3)),Var(4)),revision,limit)
        return waits + ready

    def active(self, tag, limit=4096):
        """Owned waiting/ready continuations; completed history is excluded."""
        return self._operation(lambda rev: self._active(text(tag),rev,limit))

    def registered(self, identity):
        """Inspect the immutable registration without consuming a message."""
        return self._operation(lambda rev: self._registration(text(identity),rev))

    def listen(self, channel, sender, tag):
        record=E("MailboxListener",1,*map(text,(channel,sender,tag)))
        return self._operation(lambda rev: self._plan(E("listen",record,self._rows(record,rev))))

    def pending(self, channel, sender):
        """Select one occurrence for a persistent listener, without consuming it."""
        return self._operation(lambda rev: self._one(self._rows(
            E("MailboxPending",1,text(channel),Var(0),text(sender),Var(1)),rev,first=True)))

    def accept(self, identity, message_id, channel, sender, tag, owner, generation, *, capacity):
        """Consume one message and retain its listener in the same native batch.

        Message identity is checked in full. A shortened/hash-based ticket is
        only a lookup key: a collision refuses rather than reusing authority.
        """
        identity,message_id,channel,sender,tag,owner=map(text,(identity,message_id,channel,sender,tag,owner))
        generation=activity_generation(generation)
        if type(capacity) is not int or capacity<1:
            raise ValueError("positive active-wait admission budget required")
        def operation(rev):
            message=self._one(self._rows(E("MailboxMessage",1,message_id,channel,Var(0),sender,Var(1)),rev))
            if message is None: raise MailboxError("unknown listener message")
            wait=E("MailboxWait",1,identity,channel,message[4],sender,owner,generation,E("MailboxResume",1,tag,identity))
            listener=E("MailboxListener",1,channel,sender,tag)
            old=self._rows(E("MailboxRegistration",1,identity,Var(0)),rev)
            if not old and len(self._active(tag,rev,capacity+1))>=capacity:
                raise MailboxError("active-wait capacity; no receive committed")
            return self._plan(E("accept",listener,wait,message,
                self._rows(E("MailboxPending",1,channel,message[4],sender,message_id),rev),old,
                self._rows(E("MailboxOwner",1,owner,generation,Var(0)),rev),
                self._rows(E("MailboxDelivery",1,identity,Var(0)),rev),self._rows(listener,rev)))
        return self._operation(operation)

    def register(self, identity, channel, correlation, sender, owner, generation, continuation, *, capacity=None, intent=None):
        identity, channel, correlation, sender, owner=map(text,(identity,channel,correlation,sender,owner))
        generation=activity_generation(generation)
        if not isinstance(continuation,tuple) or len(continuation)!=4 or continuation[:2]!=(S("MailboxResume"),1):
            raise ValueError("versioned inert MailboxResume required")
        text(continuation[2]); text(continuation[3])
        if capacity is not None and (type(capacity) is not int or capacity<1):
            raise ValueError("positive active-wait admission budget required")
        if intent is not None:
            if (not isinstance(intent,tuple) or len(intent)!=4 or intent[:2]!=(S("MailboxCodexWatch"),1)
                or intent[3]!=correlation or continuation[3]!=identity):
                raise ValueError("correlated inert Codex watch intent required")
            text(intent[2]); text(intent[3])
        wait=E("MailboxWait",1,identity,channel,correlation,sender,owner,generation,continuation)
        def operation(rev):
            old=self._rows(E("MailboxRegistration",1,identity,Var(0)),rev)
            if capacity is not None and not old:
                if len(self._active(continuation[2],rev,capacity+1))>=capacity:
                    raise MailboxError("active-wait capacity; no registration committed")
            request=E("register",wait,old,
                self._rows(E("MailboxOwner",1,owner,generation,Var(0)),rev),
                self._rows(E("MailboxRoute",1,channel,correlation,sender,Var(0)),rev))
            if intent is not None:
                request=E("register-intent",*request[1:],E("MailboxIntent",1,identity,continuation[2],intent),
                          self._rows(E("MailboxIntent",1,identity,Var(0),Var(1)),rev))
            return self._plan(request)
        return self._operation(operation)

    def _registration(self, identity, revision):
        record=self._one(self._rows(E("MailboxRegistration",1,identity,Var(0)),revision))
        if record is None:
            raise MailboxError("unknown wait")
        return record[3]

    def resolve(self, identity):
        identity=text(identity)
        def operation(rev):
            wait=self._registration(identity,rev)
            _, _, _, channel, correlation, sender, owner, generation, continuation=wait
            ready=self._one(self._rows(E("MailboxReady",1,identity,owner,generation,continuation,Var(0)),rev))
            if ready is not None:
                message=self._one(self._rows(E("MailboxMessage",1,ready[6],channel,correlation,sender,Var(0)),rev))
                if message is None:
                    raise MailboxError("ready message is missing")
                return E("MailboxResult",1,identity,owner,generation,continuation,message)
            settled=self._one(self._rows(E("MailboxSettled",1,identity,owner,generation,Var(0)),rev))
            if settled is not None:
                return settled
            active=self._one(self._rows(wait,rev))
            cancelled=self._one(self._rows(E("MailboxCancelled",1,identity,owner,generation),rev))
            pending=self._one(self._rows(E("MailboxPending",1,channel,correlation,sender,Var(0)),rev,first=True))
            if active is not None and cancelled is not None:
                raise MailboxError("conflicting mailbox terminal state")
            if pending is not None and active is not None:
                message=self._one(self._rows(E("MailboxMessage",1,pending[5],channel,correlation,sender,Var(0)),rev))
                if message is None:
                    raise MailboxError("pending message is missing")
                self._plan(E("receive",active,pending,message))
                return E("MailboxResult",1,identity,owner,generation,continuation,message)
            if cancelled is not None:
                if pending is not None:
                    route=E("MailboxRoute",1,channel,correlation,sender,identity)
                    if self._rows(route,rev)!=(route,):
                        raise MailboxError("cancelled route is missing")
                    self._plan(E("late",cancelled,route,pending))
                return cancelled
            if active is None:
                raise MailboxError("missing wait state")
            return E("MailboxWaiting",1,identity,owner,generation)
        return self._operation(operation)

    def cancel(self, identity, owner, generation):
        identity,owner=text(identity),text(owner)
        generation=activity_generation(generation)
        def operation(rev):
            wait=self._registration(identity,rev)
            if wait[6:8]!=(owner,generation):
                raise MailboxError("activity authority mismatch")
            active=self._one(self._rows(wait,rev))
            if active is not None:
                return self._plan(E("cancel",active,owner,generation))
            # A completed receive won the race. Report actual state; cancel
            # does not retract a delivered continuation or an external action.
            return self.resolve(identity)
        return self._operation(operation)

    def settle(self, identity, owner, generation):
        identity,owner=text(identity),text(owner)
        generation=activity_generation(generation)
        def operation(rev):
            wait=self._registration(identity,rev)
            if wait[6:8]!=(owner,generation):
                raise MailboxError("activity authority mismatch")
            ready=self._one(self._rows(E("MailboxReady",1,identity,owner,generation,wait[8],Var(0)),rev))
            if ready is not None:
                return self._plan(E("settle",ready,owner,generation))
            done=self._one(self._rows(E("MailboxSettled",1,identity,owner,generation,Var(0)),rev))
            if done is None:
                raise MailboxError("no ready continuation")
            return "settled"
        return self._operation(operation)

    def await_result(self, identity, wait_ms=30000, reconnect_seconds=30):
        # Capture before inspection. A commit between the query and watch is
        # detected immediately by the owner; registering a watch is atomic
        # with its revision comparison. After a lost reply, inspect the same
        # durable owner again; a receive may already have committed there.
        if wait_ms<1 or reconnect_seconds<0:
            raise ValueError("positive wait and nonnegative reconnect budget required")
        while True:
            try:
                self.space.describe()
                captured=self.space.revision
                result=self.resolve(identity)
                if result[0]!=S("MailboxWaiting"):
                    return result
                self.metrics["notifications"]+=1
                timeout=self.client.timeout
                try:
                    self.client.timeout=max(timeout,wait_ms/1000+5)
                    self.space.watch(captured,wait_ms)
                finally:
                    self.client.timeout=timeout
            except (OSError,Uncertain):
                deadline=time.monotonic()+reconnect_seconds
                delay=.02
                while True:
                    try:
                        self.reconnect(); break
                    except (OSError,Uncertain):
                        if time.monotonic()>=deadline: raise
                        time.sleep(min(delay,max(0,deadline-time.monotonic())))
                        delay=min(1,delay*2)

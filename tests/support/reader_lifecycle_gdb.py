"""GDB-only r82 snapshot lifecycle probe; no inferior calls or data writes.

The parent checks the executable hash. Offsets below come from that binary's
instructions, not the evolving worktree layout. Software breakpoints affect
only the owned child. This is sequential observation, not a race detector.
"""
import gdb
import json
import os
import resource
import shlex

config = json.loads(os.environ["CETTA_READER_GDB_CONFIG"])
layout = config["layout"]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
events, errors, pending = [], [], set()
registered, root_calls, collections = {}, {}, {}
exit_status = {}
next_call = 0
cache_generation = 0
sampled = set()
skipped_collections = 0
root_instrumentation = []


def word(address, size=8):
    return int.from_bytes(gdb.selected_inferior().read_memory(address, size).tobytes(), "little")


def vector(address, count):
    if count > 128:
        raise ValueError("snapshot exceeds admitted probe size")
    return [word(address + 8 * i) for i in range(count)]


def snapshot(address):
    arity, count = word(address + 4, 4), word(address + layout["count"])
    if arity > 8 or count > 32:
        raise ValueError("snapshot exceeds admitted probe layout")
    head, columns, records, spine = (word(address + off) for off in
                                     (layout["head"], 40, 48, 56))
    nodes = word(spine, 4) if spine else 0
    if nodes > 128:
        raise ValueError("skeleton exceeds admitted probe layout")
    constructors = [word(spine + 16 + 16 * i) for i in range(nodes) if word(spine + 8 + 16 * i, 4)]
    column_ids = vector(columns, count * arity)
    cache = vector(records, count) if records else []
    return {"context": address, "universe": word(address + 8), "head": head,
            "arity": arity, "count": count, "spine": spine, "columns": column_ids,
            "cache": cache, "expected_roots": [head] + constructors + column_ids +
            [key for key in cache if key != (1 << 64) - 1]}


def frame_key(frame):
    caller = frame.older()
    return (int(caller.pc()), int(caller.read_register("rsp"))) if caller else (0, 0)


def record(kind, **fields):
    global next_call
    if len(events) >= config["event_limit"]:
        raise ValueError("event-limit")
    next_call += 1
    event = {"kind": kind, "call": next_call, **fields}
    events.append(event)
    return event


class Returned(gdb.FinishBreakpoint):
    def __init__(self, frame, event, action):
        super().__init__(frame, internal=True)
        self.event, self.action = event, action
        pending.add(self)

    def stop(self):
        try:
            result = int(gdb.parse_and_eval("$rax")) & ((1 << 64) - 1)
            self.event["returned"] = True
            self.event["result"] = result
            self.action(result)
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
        pending.discard(self)
        return False

    def out_of_scope(self):
        self.event["unwound"] = True
        errors.append("unwound:" + self.event["kind"])
        pending.discard(self)


class Entered(gdb.Breakpoint):
    def __init__(self, symbol):
        super().__init__("*" + symbol, internal=True)
        self.symbol = symbol

    def stop(self):
        global skipped_collections
        try:
            frame = gdb.newest_frame()
            arg = lambda name: int(frame.read_register(name))
            key = frame_key(frame)
            if self.symbol in ("term_universe_add_root_provider", "term_universe_remove_root_provider"):
                # Resolve after the PIE executable has loaded; a link-time
                # address obtained before run would miss every live provider.
                callback = int(gdb.parse_and_eval("(void *)space_kept_occurrences_roots"))
                if arg("rsi") != callback:
                    return False
                context = arg("rdx")
                view = snapshot(context)
                if view["universe"] != arg("rdi"):
                    errors.append("provider-universe-mismatch")
                adding = self.symbol == "term_universe_add_root_provider"
                event = record("register" if adding else "unregister", **view,
                               registry_len=word(arg("rdi") + layout["registry_len"]),
                               registry_cap=word(arg("rdi") + layout["registry_cap"]))

                def finish(value):
                    if adding:
                        if value & 255:
                            registered[context] = view["universe"]
                    else:
                        if context not in registered:
                            errors.append("unregister-without-registration")
                        registered.pop(context, None)
                Returned(frame, event, finish)
            elif self.symbol == "space_kept_occurrences_roots":
                view = snapshot(arg("rcx"))
                event = record("roots", **view, actual_roots=[], collections=list(collections))
                root_calls[key] = event

                def finish(_):
                    event["exact"] = event["actual_roots"] == event["expected_roots"]
                    root_calls.pop(key, None)
                Returned(frame, event, finish)
            elif self.symbol == "space_kept_occurrences_id":
                context, index = arg("rdi"), arg("rsi")
                view = snapshot(context)
                if index >= view["count"]:
                    raise ValueError("cache index outside snapshot")
                event = record("record-read", context=context, index=index, before=view["cache"][index])

                def finish(value):
                    global cache_generation
                    event["after"] = snapshot(context)["cache"][index]
                    event["matches_cache"] = value == event["after"]
                    event["stable_existing"] = event["before"] == (1 << 64) - 1 or event["before"] == value
                    if event["before"] == (1 << 64) - 1 and value != event["before"]:
                        cache_generation += 1
                Returned(frame, event, finish)
            else:
                universe = arg("rdi")
                contexts = [ctx for ctx, owner in registered.items() if owner == universe]
                sample = (universe, tuple(sorted(contexts)), cache_generation)
                if not contexts or sample in sampled:
                    skipped_collections += 1
                    return False
                sampled.add(sample)
                if collections:
                    raise ValueError("nested collection outside sequential probe contract")
                event = record("collect", universe=universe, active_contexts=contexts)
                collections[event["call"]] = event
                for breakpoint in root_instrumentation:
                    breakpoint.enabled = True

                def finish(value):
                    event["success"] = bool(value & 255)
                    collections.pop(event["call"], None)
                    for breakpoint in root_instrumentation:
                        breakpoint.enabled = False
                Returned(frame, event, finish)
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
            self.enabled = False
        return False


class Visit(gdb.Breakpoint):
    def __init__(self, offset):
        super().__init__("*space_kept_occurrences_roots+" + hex(offset), internal=True)

    def stop(self):
        try:
            frame = gdb.newest_frame()
            event = root_calls.get(frame_key(frame))
            if event is None:
                raise ValueError("root-visit-without-callback")
            event["actual_roots"].append(int(frame.read_register("rsi")) & ((1 << 64) - 1))
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
            self.enabled = False
        return False


gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set disable-randomization off")
gdb.execute("set print thread-events off")
for symbol in ("term_universe_add_root_provider", "term_universe_remove_root_provider",
               "space_kept_occurrences_roots", "space_kept_occurrences_id", "term_universe_collect"):
    breakpoint = Entered(symbol)
    if symbol == "space_kept_occurrences_roots":
        root_instrumentation.append(breakpoint)
for offset in (0x21, 0x48, 0x7f, 0xad):
    root_instrumentation.append(Visit(offset))
for breakpoint in root_instrumentation:
    breakpoint.enabled = False


def exited(event):
    exit_status.update(code=getattr(event, "exit_code", None), terminal=True)


gdb.events.exited.connect(exited)
try:
    gdb.execute("run " + gdb.parameter("args") + " > " + shlex.quote(config["stdout"]) +
                " 2> " + shlex.quote(config["stderr"]))
except gdb.error as error:
    errors.append(str(error))
with open(config["receipt"], "w") as stream:
    json.dump({"events": events, "errors": errors, "pending": len(pending),
               "registered_at_exit": list(registered), "exit": exit_status,
               "skipped_collections": skipped_collections,
               "sampling": "first collection per universe, active-context set and cache generation",
               "architecture": gdb.selected_inferior().architecture().name()}, stream, indent=2)

"""Run only inside GDB: bounded, read-only native-route observation.

No inferior functions are called and no inferior data/registers are written.
Software breakpoints temporarily change instruction bytes in the private
debugged process. The parent pins the ELF hash and compares its answers with
an uninstrumented run. This is not a timing or concurrency qualification.
"""
import gdb
import json
import os
import shlex

config = json.loads(os.environ["CETTA_OBSERVER_GDB_CONFIG"])
events, errors, active, pending = [], [], {}, set()
fold_active = {}
next_call = 0
exit_status = {}


def frame_key(frame):
    caller = frame.older()
    return (int(caller.pc()), int(caller.read_register("rsp"))) if caller else (0, 0)


def append(event):
    if len(events) >= config["event_limit"]:
        if "event-limit" not in errors:
            errors.append("event-limit")
        return False
    events.append(event)
    return True


class Returned(gdb.FinishBreakpoint):
    def __init__(self, frame, record, key):
        super().__init__(frame, internal=True)
        self.record, self.key = record, key
        pending.add(self)

    def stop(self):
        try:
            value = int(gdb.parse_and_eval("$rax")) & ((1 << 64) - 1)
            self.record.update(return_u64=value, return_u32=value & 0xffffffff, return_u8=value & 255)
            if self.record.get("out_pointer"):
                self.record["out_bool"] = int.from_bytes(gdb.selected_inferior().read_memory(
                    self.record["out_pointer"], 1).tobytes(), "little")
        except gdb.error as error:
            errors.append(str(error))
        if active.get(self.key) == self.record["call"]:
            active.pop(self.key)
        if fold_active.get(self.key) == self.record["call"]:
            fold_active.pop(self.key)
        pending.discard(self)
        return False

    def out_of_scope(self):
        self.record["unwound_without_return"] = True
        if active.get(self.key) == self.record["call"]:
            active.pop(self.key)
        if fold_active.get(self.key) == self.record["call"]:
            fold_active.pop(self.key)
        pending.discard(self)


class Entered(gdb.Breakpoint):
    def __init__(self, symbol):
        # Instruction entry keeps the x86-64 ABI argument registers intact.
        super().__init__("*" + symbol, internal=True)
        self.symbol = symbol

    def stop(self):
        global next_call
        try:
            frame = gdb.newest_frame()
            key = frame_key(frame)
            next_call += 1
            record = {"kind": "function", "symbol": self.symbol, "call": next_call,
                      "caller_pc": key[0], "caller_sp": key[1], "entry_pc": int(frame.pc())}
            if self.symbol == "cetta_fold_big_start":
                ancestor = frame.older()
                while ancestor:
                    # The reviewed wrapper tail-jumps to native_fold_route.
                    # Its caller coordinate survives even when its name does not.
                    owner = fold_active.get(frame_key(ancestor))
                    if owner is not None:
                        record["fold_owner"] = owner
                        break
                    ancestor = ancestor.older()
            if not append(record):
                self.enabled = False
                return False
            if self.symbol == "cetta_native_query_run":
                pointer = int(frame.read_register("rdi"))
                record["query_nonnull"] = bool(pointer)
                if pointer:
                    memory = gdb.selected_inferior().read_memory(pointer, 120).tobytes()
                    # r82 x86-64 CettaNativeQuery layout; pinned and independently
                    # checked against the header and its accesses in the ELF.
                    record["consumer"] = int.from_bytes(memory[32:36], "little")
                    record["effect_count"] = int.from_bytes(memory[48:56], "little")
                    record["has_guard"] = any(memory[112:120])
            if self.symbol in ("space_match_exists_ground_exact", "space_match_backend_ground_exact_exists_frontier"):
                record["out_pointer"] = int(frame.read_register("rdx"))
            if self.symbol in ("petta_eval_machine_native_collect", "cetta_native_foldall"):
                active[key] = next_call
            if self.symbol == "cetta_native_foldall":
                fold_active[key] = next_call
            Returned(frame, record, key)
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
        return False


class Checkpoint(gdb.Breakpoint):
    def __init__(self, name, offset, register):
        super().__init__("*petta_eval_machine_native_collect+" + hex(offset), internal=True)
        self.name, self.register = name, register

    def stop(self):
        try:
            frame = gdb.newest_frame()
            owner = active.get(frame_key(frame))
            record = {"kind": "checkpoint", "name": self.name, "owner": owner,
                      "pc": int(frame.pc())}
            if self.register:
                record["value"] = int(gdb.parse_and_eval("$" + self.register)) & ((1 << 64) - 1)
            if owner is None:
                errors.append("checkpoint-without-owner:" + self.name)
            if not append(record):
                self.enabled = False
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
        return False


gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set disable-randomization off")
gdb.execute("set print thread-events off")
for symbol in config["symbols"]:
    try:
        Entered(symbol)
    except gdb.error as error:
        errors.append(str(error))
for checkpoint in config["checkpoints"]:
    try:
        Checkpoint(**checkpoint)
    except gdb.error as error:
        errors.append(str(error))


def exited(event):
    exit_status["code"] = getattr(event, "exit_code", None)


gdb.events.exited.connect(exited)
try:
    gdb.execute("run " + gdb.parameter("args") + " > " + shlex.quote(config["stdout"]) +
                " 2> " + shlex.quote(config["stderr"]))
except gdb.error as error:
    errors.append(str(error))
with open(config["receipt"], "w") as stream:
    json.dump({"events": events, "errors": errors, "pending": len(pending),
               "exit": exit_status, "architecture": gdb.selected_inferior().architecture().name()}, stream, indent=2)

"""Read-only r82 caller-domain probe; the parent pins the executable hash.

No inferior functions, data or registers are changed. Software breakpoints
affect only the owned child. Each Boolean output belongs to the completed
call that supplied it; void functions have no interpreted return value.
"""
import gdb
import json
import os
import resource
import shlex

config = json.loads(os.environ["CETTA_EXISTENCE_DOMAINS_GDB"])
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
events, errors, pending, active = [], [], set(), []
exit_status = {}


class Returned(gdb.FinishBreakpoint):
    def __init__(self, frame, event, outputs, boolean):
        super().__init__(frame, internal=True)
        self.event, self.outputs, self.boolean = event, outputs, boolean
        pending.add(self)

    def stop(self):
        try:
            self.event["returned"] = True
            if self.boolean:
                self.event["result"] = int(gdb.parse_and_eval("$rax")) & 255
            for name, address, width in self.outputs:
                self.event[name] = int.from_bytes(gdb.selected_inferior().read_memory(address, width).tobytes(), "little")
            if not active or active[-1] != self.event["call"]:
                errors.append("non-nested-call-return")
            else:
                active.pop()
        except gdb.error as error:
            errors.append(str(error))
        pending.discard(self)
        return False

    def out_of_scope(self):
        errors.append("unwound:" + self.event["symbol"])
        pending.discard(self)


class Entered(gdb.Breakpoint):
    def __init__(self, symbol, fields, boolean=True):
        super().__init__("*" + symbol, internal=True)
        self.symbol, self.fields, self.boolean = symbol, fields, boolean

    def stop(self):
        try:
            if len(events) >= config["event_limit"]:
                raise ValueError("event-limit")
            frame = gdb.newest_frame()
            event = {"symbol": self.symbol, "space": int(frame.read_register("rdi")),
                     "caller": frame.older().name(), "call": len(events),
                     "parent": active[-1] if active else None, "void": not self.boolean}
            outputs = [(name, int(frame.read_register(register)), width) for name, register, width in self.fields]
            events.append(event)
            active.append(event["call"])
            Returned(frame, event, outputs, self.boolean)
        except (gdb.error, ValueError) as error:
            errors.append(str(error))
            self.enabled = False
        return False


gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set disable-randomization off")
gdb.execute("set print thread-events off")
Entered("space_match_exists_flat_linear_view64", [("found", "r8", 1), ("examined", "r9", 8)])
Entered("space_dense_count_columns", [("count", "rcx", 8)])
Entered("space_match_exists_ground_exact_expression_coordinates", [("applicable", "rcx", 1)])
Entered("space_contains_only_exact_atoms", [])
Entered("space_dense_settle_for_pattern", [], boolean=False)


def exited(event):
    exit_status.update(code=getattr(event, "exit_code", None), terminal=True)


gdb.events.exited.connect(exited)
try:
    gdb.execute("run " + gdb.parameter("args") + " > " + shlex.quote(config["stdout"]) +
                " 2> " + shlex.quote(config["stderr"]))
except gdb.error as error:
    errors.append(str(error))
with open(config["receipt"], "w") as stream:
    json.dump({"events": events, "errors": errors, "pending": len(pending), "active": active,
               "exit": exit_status, "architecture": gdb.selected_inferior().architecture().name()}, stream, indent=2)

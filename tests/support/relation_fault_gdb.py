"""GDB-only fault injection at reviewed allocator call sites in a private child.

Unlike the read-only coverage probe, this deliberately sets RAX and skips one
five-byte CALL to simulate its NULL result. No allocator call is made at that
site, so realloc's old allocation remains intact. This is a NULL-return model,
not arbitrary allocator/errno or concurrency fault simulation.
"""
import gdb
import json
import os
import resource
import shlex

config = json.loads(os.environ["CETTA_RELATION_FAULT_CONFIG"])
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
events, errors, signals, exit_status = [], [], [], {}


class Fault(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*'" + config["symbol"] + "'+" + hex(config["offset"]), internal=True)
        self.matched = 0

    def stop(self):
        try:
            frame = gdb.newest_frame()
            stack = []
            ancestor = frame
            while ancestor and len(stack) < 24:
                stack.append(ancestor.name())
                ancestor = ancestor.older()
            if config.get("ancestor") and config["ancestor"] not in stack:
                return False
            matched_symbols = {}
            for register, symbol in config.get("symbol_registers", {}).items():
                address = int(gdb.parse_and_eval("(void *)" + symbol))
                if int(frame.read_register(register)) != address:
                    return False
                matched_symbols[register] = {"symbol": symbol, "address": address}
            self.matched += 1
            if self.matched != config["nth"]:
                return False
            pc = int(frame.pc())
            instruction = gdb.selected_inferior().read_memory(pc, 5).tobytes()
            if instruction[0] != 0xe8:
                errors.append("site-is-not-relative-call")
                return True
            record = {"site": config["name"], "pc": pc, "instruction": instruction.hex(),
                           "matched": self.matched, "stack": stack,
                           "matched_symbol_registers": matched_symbols,
                           "rdi": int(frame.read_register("rdi")), "rsi": int(frame.read_register("rsi"))}
            if config["name"] == "binary-holds-grow":
                # Reviewed r82 layout: R12 holds the relation here; its first
                # side's row_len pointer is at offset 56. Number zero is the
                # first inserted key in these controls.
                relation = int(frame.read_register("r12"))
                memory = gdb.selected_inferior()
                lengths = int.from_bytes(memory.read_memory(relation + 56, 8).tobytes(), "little")
                length = int.from_bytes(memory.read_memory(lengths, 4).tobytes(), "little")
                record["row0_kind"] = "bits" if length & 0x80000000 else "array"
            events.append(record)
            gdb.execute("set $rax = 0")
            gdb.execute("set $pc = " + str(pc + 5))
            self.enabled = False
        except gdb.error as error:
            errors.append(str(error))
            return True
        return False


def stopped(event):
    if isinstance(event, gdb.SignalEvent):
        signals.append(event.stop_signal)


def exited(event):
    exit_status["code"] = getattr(event, "exit_code", None)
    exit_status["terminal"] = True


gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set disable-randomization off")
gdb.execute("set print thread-events off")
gdb.events.stop.connect(stopped)
gdb.events.exited.connect(exited)
fault = Fault()
try:
    gdb.execute("run " + gdb.parameter("args") + " > " + shlex.quote(config["stdout"]) +
                " 2> " + shlex.quote(config["stderr"]))
    # Let the process receive an observed fatal signal, rather than reporting
    # a merely stopped child as a completed termination.
    if not exit_status.get("terminal") and signals and signals[-1] in ("SIGABRT", "SIGSEGV", "SIGBUS"):
        gdb.execute("continue")
except gdb.error as error:
    errors.append(str(error))
with open(config["receipt"], "w") as output:
    json.dump({"events": events, "errors": errors, "signals": signals, "exit": exit_status,
               "matches": fault.matched, "architecture": gdb.selected_inferior().architecture().name()}, output, indent=2)

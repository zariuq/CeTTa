"""Hash-pinned x86-64 route evidence for reviewed observer binaries.

Offsets below are instruction boundaries in petta_eval_machine_native_collect
(r82 ELF symbol 0x455060; successor candidate 0x456a60). The four checkpoint
offsets and their surrounding instructions agree in both reviewed disassemblies.
Inlined paths require their own completion evidence, not a shared collector's
success or merely entry into a recognizer. Unknown builds decline.
"""
import json
from pathlib import Path

R82_SHA256 = "2acbee5636155b7b175c5965e3fb6d0fbc12b50064f56b39f018a59c2c2d4aed"
SUCCESSOR_CANDIDATE_SHA256 = "2f00d0c875c00bac886ff6cff93a883d12a1b97f4939bed24354bd1eaa130b37"
SUCCESSOR_CANDIDATE_2_SHA256 = "4f5bd550a4c723e1c407cd81f4716f8b34ca17e69467b48d1eea60c44108d131"
SCOPED_OWNERS_SHA256 = "d89724427430ecaae39ec4e712a711aa6748cea36c2af6a0b081dd3e5ad9e3c0"
REVIEWED_SHA256S = (R82_SHA256, SUCCESSOR_CANDIDATE_SHA256,
                    SUCCESSOR_CANDIDATE_2_SHA256, SCOPED_OWNERS_SHA256)
COLLECT = "petta_eval_machine_native_collect"
REQUIRED_SUBROUTES = {"arithmetic-fold": ("ordered-scan", "run", "moments", "bigint")}
FUNCTIONS = {
    "scalar-fold": ("petta_eval_scalar_fold",),
    "filtered-match": ("petta_eval_machine_native_filter",),
    "arithmetic-fold": ("cetta_native_foldall",),
    "relaxation": ("petta_relaxation_region_run",),
    "open-join": ("cetta_native_open_query_run",),
    "guard-batch": ("cetta_native_query_run",),
    "kept-relation": ("space_dense_take_publishing",),
    "match-existence": ("space_match_exists_ground_exact",),
}
CHECKPOINTS = {
    # Immediately after the inlined group's final collector call, before
    # restoring its two stack arguments. EAX is the group's actual status.
    "grouped-count": ({"name": "group-result", "offset": 0x370a, "register": "eax"},),
    # Immediately after the inlined listed match's joined collector call.
    "listed-match": ({"name": "listed-result", "offset": 0x2777, "register": "eax"},),
    # The saturation loop ended successfully; result allocation follows.
    # Its enclosing invocation must also return DONE, not fail allocation.
    "saturation": ({"name": "saturation-finished", "offset": 0x2048, "register": None},),
    # At this test RBP is the static-premise candidate count. Zero marks a
    # fire as dead. Require saturation success in the SAME invocation.
    "dead-rule": (
        {"name": "dead-premise-count", "offset": 0x1d63, "register": "rbp"},
        {"name": "saturation-finished", "offset": 0x2048, "register": None},
    ),
}


def probe_config(family, identity, directory):
    if identity not in REVIEWED_SHA256S:
        raise ValueError("no reviewed debugger probe profile for this binary hash")
    symbols = list(FUNCTIONS.get(family, ()))
    checkpoints = list(CHECKPOINTS.get(family, ()))
    if checkpoints:
        symbols.append(COLLECT)
    if family in ("open-join", "listed-match"):
        symbols.extend(symbol for symbol in (COLLECT, "cetta_native_query_run") if symbol not in symbols)
    if family == "arithmetic-fold":
        symbols.append("cetta_fold_big_start")
    if family == "match-existence":
        symbols.extend(("space_contains_exact", "space_contains_only_exact_atoms", "space_exact_frontier_certified",
                        "space_match_backend_ground_exact_exists_frontier"))
    if not symbols:
        return None  # Take-over already has a direct operation trace.
    return {"symbols": symbols, "checkpoints": checkpoints, "event_limit": 20000,
            "stdout": str(directory / "inferior.stdout"), "stderr": str(directory / "inferior.stderr"),
            "receipt": str(directory / "probe.json")}


def valid_probe(receipt):
    return (receipt.get("architecture") == "i386:x86-64" and receipt.get("exit", {}).get("code") == 0
            and not receipt.get("errors") and receipt.get("pending") == 0
            and all(not event.get("unwound_without_return") for event in receipt.get("events", ())))


def completed_routes(family, receipt):
    if not valid_probe(receipt):
        return []
    events = receipt["events"]
    if family in FUNCTIONS:
        accepted = []
        for event in events:
            if event.get("symbol") not in FUNCTIONS[family]:
                continue
            status = event.get("return_u32")
            success = status in (2, 3, 4) if family == "arithmetic-fold" else status == 1
            if family == "kept-relation":
                success = event.get("return_u8") == 1
            if family == "match-existence":
                success = event.get("out_bool") == 1  # A proved absence is also a completed observation.
            if family == "guard-batch":
                success = success and event.get("has_guard") and event.get("effect_count", 0) > 0 and event.get("consumer") == 1
            if success:
                accepted.append(event["call"])
        return accepted
    completed = {event["call"] for event in events
                 if event.get("symbol") == COLLECT and event.get("return_u32") == 1}
    checkpoints = [event for event in events if event.get("kind") == "checkpoint" and event.get("owner") in completed]
    if family in ("grouped-count", "listed-match"):
        name = "group-result" if family == "grouped-count" else "listed-result"
        return sorted({event["owner"] for event in checkpoints if event["name"] == name and event.get("value") == 1})
    saturated = {event["owner"] for event in checkpoints if event["name"] == "saturation-finished"}
    if family == "saturation":
        return sorted(saturated)
    if family == "dead-rule":
        return sorted(saturated & {event["owner"] for event in checkpoints
                      if event["name"] == "dead-premise-count" and event.get("value") == 0})
    return []


def debugger_coverage(family, native, reference, native_law, reference_law):
    usable = valid_probe(native) and valid_probe(reference) and native_law == reference_law == "equal"
    activated = completed_routes(family, native) if usable else []
    ordinary = completed_routes(family, reference) if usable else []
    # Representation admission is independent of the query-reference flag.
    # It may occur on both routes and needs no differential counter premise.
    storage = family == "kept-relation"
    confirmed = bool(activated) and (storage or not ordinary)
    subroutes = []
    if family == "arithmetic-fold" and confirmed:
        completed = set(activated)
        labels = {2: "ordered-scan", 3: "run", 4: "moments"}
        subroutes = sorted({labels[event["return_u32"]] for event in native["events"]
                            if event.get("call") in completed})
        if any(event.get("symbol") == "cetta_fold_big_start" and event.get("fold_owner") in completed
               for event in native["events"]):
            subroutes.append("bigint")
    return {"level": "debugger-return-confirmed" if confirmed else "unconfirmed",
            "probe_valid": usable, "native_completed": activated, "reference_completed": ordinary,
            "native_observation_law": native_law, "reference_observation_law": reference_law,
            "subroutes": subroutes,
            "scope": "Hash-pinned worker completion; no timing/concurrency claim."}


def run_probe(family, identity, command, directory, environment, execute, *, cwd, timeout, output_limit):
    directory.mkdir()
    config = probe_config(family, identity, directory)
    if config is None:
        return None
    env = dict(environment, CETTA_OBSERVER_GDB_CONFIG=json.dumps(config))
    script = Path(__file__).with_name("native_observer_gdb.py")
    debugger = execute(["gdb", "-q", "-nx", "-nh", "-batch", "-iex", "set auto-load off",
                        "-iex", "set debuginfod enabled off", "-x", str(script), "--args"] + command,
                       cwd=cwd, environment=env, timeout=timeout, output_limit=output_limit, file_limit=output_limit)
    receipt_path = Path(config["receipt"])
    try:
        receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {"errors": ["missing-receipt"]}
    except (OSError, ValueError):
        receipt = {"errors": ["invalid-receipt"]}
    receipt["debugger_status"] = debugger.status
    receipt["debugger_returncode"] = debugger.returncode
    if debugger.status != "exited" or debugger.returncode != 0:
        receipt.setdefault("errors", []).append("debugger-execution-failed")
    (directory / "debugger.stdout").write_text(debugger.stdout)
    (directory / "debugger.stderr").write_text(debugger.stderr)
    (directory / "driver.json").write_text(json.dumps({"status": debugger.status,
        "returncode": debugger.returncode, "seconds": debugger.seconds,
        "errors": receipt.get("errors", [])}, indent=2) + "\n")
    return receipt, Path(config["stdout"]), Path(config["stderr"])

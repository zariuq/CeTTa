"""GDB-side capture of native Prime receipt graphs and ancestry queries.

Run this file with GDB's ``source`` command and set
``CETTA_PRIME_RECEIPT_TRACE_OUT`` to the destination JSON path.  The capture
uses debug information only: it does not require an instrumented CeTTa build
or change the runtime representation. Optimized GCC builds are supported on
the SysV x86-64 calling convention; unoptimized builds use named arguments.
"""

from __future__ import annotations

import json
import os
import struct

import gdb


OUTPUT = os.environ.get("CETTA_PRIME_RECEIPT_TRACE_OUT")
if not OUTPUT:
    raise gdb.GdbError(
        "CETTA_PRIME_RECEIPT_TRACE_OUT must name the output JSON file"
    )

_pointer_to_id: dict[tuple[int, int], int] = {}
_frames: list[dict[str, object]] = []
_queries: list[dict[str, int]] = []
_empty_target_queries = 0
_boundary_queries = 0


def _integer(value: gdb.Value) -> int:
    return int(value)


def _pointer(value: gdb.Value) -> int:
    return int(value.cast(gdb.lookup_type("uintptr_t")))


def _address(pointer: gdb.Value | int) -> int:
    return pointer if isinstance(pointer, int) else _pointer(pointer)


def _record_frame(pointer: gdb.Value | int) -> int | None:
    address = _address(pointer)
    if address == 0:
        return None
    memory = bytes(gdb.selected_inferior().read_memory(address, 41))
    left_address, right_address, session, event_id, depth = (
        struct.unpack_from("<QQQQQ", memory)
    )
    has_event = bool(memory[40])
    key = (address, session)
    known = _pointer_to_id.get(key)
    if known is not None:
        return known

    left = _record_frame(left_address)
    right = _record_frame(right_address)
    frame_id = len(_frames)
    parents = [parent for parent in (left, right) if parent is not None]
    _pointer_to_id[key] = frame_id
    _frames.append(
        {
            "id": frame_id,
            "session": session,
            "depth": depth,
            "parents": parents,
            "event_id": event_id,
            "has_event": has_event,
        }
    )
    return frame_id


def _record_query(
    child_pointer: gdb.Value | int,
    target_pointer: gdb.Value | int,
) -> None:
    global _empty_target_queries, _boundary_queries

    target = _record_frame(target_pointer)
    child = _record_frame(child_pointer)
    if target is None:
        _empty_target_queries += 1
    elif child is None:
        _boundary_queries += 1
    else:
        _queries.append({"child": child, "target": target})


class PublicReceiptAncestorBreakpoint(gdb.Breakpoint):
    def stop(self) -> bool:
        ancestor = gdb.parse_and_eval("ancestor").dereference()
        descendant = gdb.parse_and_eval("descendant").dereference()
        ancestor_session = _integer(ancestor["session_id"])
        descendant_session = _integer(descendant["session_id"])
        ancestor_owner = _pointer(ancestor["owner"])
        descendant_owner = _pointer(descendant["owner"])

        # These are the guards in prime_need_receipt_is_ancestor before its
        # call to prime_need_receipt_reachable.
        if ancestor_session == 0 or ancestor_owner == 0:
            return False
        if (
            descendant_session == 0
            or descendant_owner == 0
            or ancestor_session != descendant_session
        ):
            return False

        _record_query(descendant["top"], ancestor["top"])
        return False


class OptimizedReceiptAncestorBreakpoint(gdb.Breakpoint):
    def stop(self) -> bool:
        # GCC's specialized helper receives target then child in the first
        # two SysV x86-64 argument registers.
        target = int(gdb.parse_and_eval("$rdi"))
        child = int(gdb.parse_and_eval("$rsi"))
        _record_query(child, target)
        return False


def _write_trace(_event: gdb.ExitedEvent) -> None:
    document = {
        "format": "cetta-prime-receipt-trace-v1",
        "frames": _frames,
        "queries": _queries,
        "empty_target_queries": _empty_target_queries,
        "boundary_queries": _boundary_queries,
    }
    with open(OUTPUT, "w", encoding="utf-8") as stream:
        json.dump(document, stream, indent=2, sort_keys=True)
        stream.write("\n")
    gdb.write(
        "PrimeReceiptTraceSummary "
        f"frames={len(_frames)} queries={len(_queries)} "
        f"empty-target={_empty_target_queries} "
        f"boundary={_boundary_queries}\n"
    )


gdb.execute("set pagination off")
gdb.execute("set debuginfod enabled off")
try:
    OptimizedReceiptAncestorBreakpoint(
        "'prime_need_receipt_is_ancestor.part.0.isra.0'",
        internal=True,
    )
except gdb.error:
    PublicReceiptAncestorBreakpoint(
        "prime_need_receipt_is_ancestor",
        internal=True,
    )
gdb.events.exited.connect(_write_trace)

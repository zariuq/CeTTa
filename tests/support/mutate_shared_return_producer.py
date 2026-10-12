#!/usr/bin/env python3
"""Check that owned producer controls reject semantic and lifetime defects."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f"mutation site occurs {source.count(before)} times")
    return source.replace(before, after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile", required=True)
    parser.add_argument("--link", required=True)
    parser.add_argument("--ldflags", required=True)
    parser.add_argument("--test-object", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()
    source_path = Path("src/shared_return_producer.c")
    source = source_path.read_text()
    original = [obj for obj in args.objects if obj.startswith("src/shared_return_producer.")]
    if len(original) != 1:
        raise ValueError("expected exactly one original shared-return producer object")
    mutations = {
        "advance-under-backpressure": [
            ("    if (!room)\n        return CETTA_SHARED_RETURN_BACKPRESSURE;\n", ""),
            ("    if (allowance > room)\n        allowance = room;\n", "    (void)room;\n"),
        ],
        "drop-pending-return": [
            ("        producer->refusal = CETTA_SHARED_RETURN_CAPTURE_REFUSED;\n        return false;",
             "        producer->execution.has_pending = false;\n"
             "        producer->refusal = CETTA_SHARED_RETURN_CAPTURE_REFUSED;\n        return false;"),
        ],
        "omit-source-check": [
            ("    if (!producer->compatible(producer->context))\n"
             "        return CETTA_SHARED_RETURN_CONTEXT_REFUSED;\n", ""),
        ],
        "omit-capture-source-check": [
            ("    if (!producer->compatible(producer->context)) {\n"
             "        if (row.release)\n            row.release(row.owner);\n"
             "        producer->refusal = CETTA_SHARED_RETURN_CONTEXT_REFUSED;\n"
             "        return false;\n    }\n", ""),
        ],
        "incomplete-is-complete": [
            ("if (producer->execution.completion == CETTA_EVAL_COMPLETE)", "if (true)"),
        ],
        "stale-buffered-delivery": [
            ("        !producer->compatible(producer->context))\n"
             "        out = (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_CONTEXT_REFUSED};",
             "        false)\n"
             "        out = (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_CONTEXT_REFUSED};"),
        ],
        "detach-drops-pending": [
            ("    *destination = producer->execution;\n",
             "    producer->execution.has_pending = false;\n"
             "    producer->execution.pending = (CettaOwnedReturn){0};\n"
             "    *destination = producer->execution;\n"),
        ],
        "cancel-peers": [
            ("    return producer && cetta_occurrence_log_cancel(producer->log, consumer);",
             "    if (!producer) return false;\n"
             "    CettaOccurrenceLogInfo info;\n"
             "    if (!cetta_occurrence_log_info(producer->log, &info)) return false;\n"
             "    for (size_t i = 0u; i < info.consumers; ++i)\n"
             "        (void)cetta_occurrence_log_cancel(producer->log, i);\n"
             "    return cetta_occurrence_log_cancel(producer->log, consumer);"),
        ],
    }
    args.out.mkdir(parents=True, exist_ok=False)
    records = []
    for name, edits in mutations.items():
        modified = source
        for before, after in edits:
            modified = replace_once(modified, before, after)
        path = args.out / (name + ".c")
        path.write_text(modified)
        obj = args.out / (name + ".o")
        binary = args.out / name
        with (args.out / (name + ".build.log")).open("x") as log:
            subprocess.run(shlex.split(args.compile) + ["-Isrc", "-c", str(path), "-o", str(obj)],
                           stdout=log, stderr=log, check=True)
            objects = [str(obj) if item == original[0] else item for item in args.objects]
            subprocess.run(shlex.split(args.link) + ["-o", str(binary), args.test_object] +
                           objects + shlex.split(args.ldflags), stdout=log, stderr=log, check=True)
        result = subprocess.run([str(binary.resolve())], capture_output=True, timeout=120)
        (args.out / (name + ".stdout")).write_bytes(result.stdout)
        (args.out / (name + ".stderr")).write_bytes(result.stderr)
        rejected = result.returncode != 0 and b"Assertion" in result.stderr
        records.append({"name": name, "exit": result.returncode, "rejected": rejected,
                        "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()})
        (args.out / "receipt.json").write_text(json.dumps(records, indent=2) + "\n")
        if not rejected:
            raise AssertionError(f"control did not reject {name} through its observable contract")
        print("PASS: shared-return producer defect rejected:", name, flush=True)
    if source_path.read_text() != source:
        raise AssertionError("production source changed during mutation checks")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Reject defects in recursive answers, regional admission and allocation recovery."""

import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess


def reject(binary, out, name, assertion):
    run = subprocess.run([str(binary.resolve())], capture_output=True, timeout=180)
    (out / (name + ".stdout")).write_bytes(run.stdout)
    (out / (name + ".stderr")).write_bytes(run.stderr)
    assert run.returncode != 0, name
    assert assertion in run.stderr, (name, run.returncode, run.stderr[-1000:])
    print("PASS: recursive producer defect rejected by observable contract:", name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile", required=True)
    parser.add_argument("--link", required=True)
    parser.add_argument("--ldflags", required=True)
    parser.add_argument("--test-object", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()
    mutations = {
        "omit-empty-read": (
            "producer_reads",
            "if (!reads || !reads->coherent)",
            "if (!reads || !reads->coherent || revision == 0u)",
            b"length == expected"),
        "ignore-changed-revision": (
            "producer_reads", "current != slot->revision", "false",
            b"length == expected"),
        "ignore-space-revision": (
            "petta_search_machine",
            "state && state->admitted &&\n        shared->mutation_epoch == space_global_mutation_epoch()",
            "state && state->admitted", b"length == 1u"),
        "retain-retired-frames": (
            "petta_search_machine",
            "if (!shared->producer_rounds)\n        return;",
            "if (!shared->producer_rounds || begin > 0u)\n        return;",
            b"probe.denied_admissions > 0u"),
        "retry-partial-capture-after-allocation-failure": (
            "petta_search_machine",
            "shared->producer_reads_failed || !entry->tarjan_on_stack",
            "!entry->tarjan_on_stack",
            b"failed.table_generator_rounds_reused == 0u"),
        "forget-static-root-read-window": (
            "petta_search_machine",
            "shared->mutation_epoch = space_global_mutation_epoch();\n        shared->root_leased = true;",
            "(void)shared;\n        shared->root_leased = true;",
            b"fresh.table_generator_rounds_reused > 0u"),
        "retain-static-answers-after-policy-switch": (
            "petta_search_machine",
            "if (table->mutation_policy == policy)\n        return true;\n    petta_table_shared_reset(table);",
            "if (table->mutation_policy == policy)\n        return true;",
            b"length == expected"),
        "omit-producer-publication-wakes": (
            "petta_search_machine", "if (*changed) {", "if (false) {",
            b"length == expected"),
        "omit-empty-producer-edge": (
            "petta_search_machine", "    (void)revision;\n    PettaProducerDependencyBuild",
            "    if (!revision) return true;\n    PettaProducerDependencyBuild",
            b"length == expected"),
    }
    sources = {module: Path(f"src/{module}.c").read_text()
               for module, *_ in mutations.values()}
    for module in ("producer_dependencies", "producer_schedule"):
        sources[module] = Path(f"src/{module}.c").read_text()
    identity = "".join(sources[key] for key in sorted(sources))
    out = Path(args.out) / hashlib.sha256(identity.encode()).hexdigest()[:16]
    out.mkdir(parents=True, exist_ok=True)
    for name, (module, before, after, rejection) in mutations.items():
        source = sources[module]
        originals = [obj for obj in args.objects if obj.startswith(f"src/{module}.")]
        assert len(originals) == 1, originals
        assert source.count(before) == 1, name
        path = out / (name + ".c")
        path.write_text(source.replace(before, after))
        obj = out / (name + ".o")
        binary = out / name
        with (out / (name + ".build.log")).open("w") as log:
            subprocess.run(shlex.split(args.compile) + ["-Isrc", "-c", str(path), "-o", str(obj)],
                           stdout=log, stderr=log, check=True)
            objects = [str(obj) if item == originals[0] else item for item in args.objects]
            subprocess.run(shlex.split(args.link) + ["-o", str(binary), args.test_object] +
                           objects + shlex.split(args.ldflags), stdout=log, stderr=log, check=True)
        reject(binary, out, name, rejection)

    standalone_mutations = {
        "omit-inline-read-during-spill": (
            "producer_reads", "tests/test_producer_reads.c",
            "} else if (reads->length) {", "} else if (false) {",
            b"!cetta_producer_reads_valid(&reads, revision, values)"),
        "overwrite-inline-first-revision": (
            "producer_reads", "tests/test_producer_reads.c",
            "reads->coherent = reads->storage.single.revision == revision;",
            "reads->storage.single.revision = revision; reads->coherent = true;",
            b"!reads.coherent"),
        "omit-reverse-reader-link": (
            "producer_dependencies", "tests/test_producer_dependencies.c",
            "source->outgoing = target->incoming = index;", "source->outgoing = index;",
            b"observation.seen[source] == reference[source][target]"),
        "retain-reader-after-retirement": (
            "producer_dependencies", "tests/test_producer_dependencies.c",
            "    cetta_producer_dependencies_clear_reads(graph, vertex);", "    (void)vertex;",
            b"Assertion"),
        "discard-reusable-edge-slot": (
            "producer_dependencies", "tests/test_producer_dependencies.c",
            "    graph->free_edge = index;", "    graph->free_edge = NO_EDGE;",
            b"allocation == before"),
        "visit-reader-in-current-pass": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "bool phase = rank < schedule->cursor ? !schedule->phase : schedule->phase;",
            "bool phase = schedule->phase;", b"schedule->storage.one.phase == schedule->phase"),
        "ignore-pass-priority": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "advance = !*word(schedule, schedule->levels - 1u, 0u, schedule->phase);",
            "advance = !!*word(schedule, schedule->levels - 1u, 0u, !schedule->phase);",
            b"actual == expected"),
        "omit-duplicate-wake-coalescing": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "if (schedule->storage.one.rank == rank)\n            return true;",
            "if (schedule->storage.one.rank == rank) { schedule->length++; return true; }",
            b"duplicate.length == 1u"),
        "lose-visited-pass-boundary": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "schedule->cursor = first + 1u;",
            "schedule->cursor = first;", b"schedule.cursor == 4096u"),
        "omit-prior-reader-during-promotion": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "    mark(schedule, prior, phase);", "    (void)prior; (void)phase;",
            b"actual[index] == expected[index]"),
        "omit-nonempty-word-summary": (
            "producer_schedule", "tests/test_producer_schedule.c",
            "        if (old)\n            return;",
            "        if (old || level == 0u)\n            return;",
            b"actual[index] == expected[index]"),
    }
    for name, (module, driver, before, after, rejection) in standalone_mutations.items():
        source = sources[module]
        assert source.count(before) == 1, name
        path = out / (name + ".c")
        path.write_text(source.replace(before, after))
        binary = out / name
        with (out / (name + ".build.log")).open("w") as log:
            subprocess.run(shlex.split(args.compile) + ["-Isrc", driver,
                           str(path), "-Wl,--wrap=calloc", "-o", str(binary)] +
                           shlex.split(args.ldflags), stdout=log, stderr=log, check=True)
        reject(binary, out, name, rejection)


if __name__ == "__main__":
    main()

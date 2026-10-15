#!/usr/bin/env python3
"""Separate filesystem observation phases on fixed inputs and matched binaries."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def ticks(count):
    return "(LCons tick " * count + "LNil" + ")" * count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", action="append", type=Path, required=True)
    parser.add_argument("--language", action="append", choices=("he", "petta"))
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/filesystem-phases")
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--long-line", action="store_true")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if args.bytes < 1 or args.repetitions < 1:
        parser.error("input size and repetitions must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tree = output / "tree"
    source, scripts = tree / "src", tree / "scripts"
    source.mkdir(parents=True, exist_ok=True)
    scripts.mkdir(parents=True, exist_ok=True)
    data = source / "input.txt"
    unit = b"ordinary line\n"
    payload = (unit * (args.bytes // len(unit) + 1))[:args.bytes]
    if args.long_line:
        payload = b"x" * args.bytes
    data.write_bytes(payload)
    directory = output / "entries"
    directory.mkdir(exist_ok=True)
    for index in range(128):
        (directory / f"entry-{index:03d}").touch()
    checker = scripts / "check_precise_vocabulary.metta"
    shutil.copyfile(ROOT / "scripts/check_precise_vocabulary.metta", checker)
    imports = "!(import! &self fs)\n!(import! &self str)\n!(import! &self system)\n"
    read = f'(fs:read-chunk $owner {args.bytes})'
    def span(body):
        return (f'(let $owner (fs:open-read {json.dumps(str(data))}) '
                f'(case {read} (((fs:chunk $bytes $offset) '
                f'(let $closed (fs:close $owner) {body})))))')

    def timed(body):
        return f'(let $start (system:monotonic-ns) {body})'

    def emit(phase, count):
        return f'(let $end (system:monotonic-ns) (println! (FilesystemPhase {phase} {count} (- $end $start))))'
    programs = {
        "startup": imports + '!(println! (FilesystemPhase startup 1 0))\n',
        "enumeration": imports + f'''(= (bench:entries $path)
 (let $owner (fs:open-directory $path) (let $space (context-space)
 (let $start (system:monotonic-ns) (case (_minimal-foldl-llist {ticks(128)} 0 $acc $tick
 (case (fs:next-entry $owner) (((fs:entry $name $hint $index) (+ $acc 1))
 ((fs:end $count) $acc))) $space)
 (((return $count) (let $closed (fs:close $owner) {emit('enumeration', '$count')}))))))))
!(bench:entries {json.dumps(str(directory))})\n''',
        "reading": imports + '!' + timed(span('(let $count (str:byte-length $bytes) '
            + emit("reading", '$count') + ')')) + '\n',
        "decoding": imports + '!' + span('(let $decoder (str:utf8-new strict) '
            + timed('(case (str:utf8-feed $decoder $bytes True) (((Utf8DecodedV1 $text $state) '
                    + '(let $count (str:byte-length $text) ' + emit("decoding", '$count') + '))))')
            + ')') + '\n',
        "line-advancement": imports + '!' + span('(let $initial (str:line-new) '
            + timed('(case (str:advance-lines $initial $bytes True) (((LineBufferV1 $fragments $number $cr) '
                    + emit("line-advancement", '$number') + ')))') + ')') + '\n',
        "matching": imports + '!' + span(timed('(case (str:find-ascii-uncased $bytes "absent") '
            + '(((TextNotFound $literal) ' + emit("matching", str(args.bytes)) + ')))')) + '\n',
    }
    files = {phase: output / f"{phase}.metta" for phase in programs}
    for phase, text in programs.items():
        files[phase].write_text(text, encoding="utf-8")
    files["whole-tool"] = checker
    records = []
    for binary in args.cetta:
        binary = binary.resolve()
        for language in args.language or ("he", "petta"):
            for phase, script in files.items():
                for repetition in range(args.repetitions):
                    name = f"{binary.name}-{language}-{phase}-{repetition}"
                    clock = output / (name + ".time")
                    command = ["/usr/bin/time", "-f", "%e %M", "-o", str(clock),
                               str(binary), "--lang", language, "--profile", "extended",
                               "--no-results", str(script)]
                    result = subprocess.run(command, cwd=output, capture_output=True)
                    (output / (name + ".stdout")).write_bytes(result.stdout)
                    (output / (name + ".stderr")).write_bytes(result.stderr)
                    wall, peak = clock.read_text().strip().splitlines()[-1].split()
                    sample = re.search(rb'FilesystemPhase\s+[^\s()]+\s+(\d+)\s+(\d+)', result.stdout)
                    valid = result.returncode == 0 and (sample is not None or
                        phase == "whole-tool" and result.stdout.startswith(b"PASS:"))
                    expected = {"startup": 1, "enumeration": 128,
                                "line-advancement": len(payload.splitlines()) + 1,
                                "reading": args.bytes, "decoding": args.bytes,
                                "matching": args.bytes}
                    if sample:
                        valid = valid and int(sample[1]) == expected[phase]
                    row = {"binary": binary.name, "language": language, "phase": phase,
                           "repetition": repetition, "bytes": args.bytes,
                           "long_line": args.long_line,
                           "exit": result.returncode, "validated": valid,
                           "wall_seconds": float(wall), "peak_rss_kib": int(peak),
                           "operation_nanoseconds": int(sample[2]) if sample else None,
                           "observed_count": int(sample[1]) if sample else None}
                    records.append(row)
                    print(json.dumps(row), flush=True)
    (output / "measurements.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    if not all(record["validated"] for record in records):
        raise SystemExit(1)


if __name__ == "__main__":
    main()

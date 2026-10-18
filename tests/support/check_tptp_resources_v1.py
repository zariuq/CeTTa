#!/usr/bin/env python3
"""TPTP asset lookup and SZS observations outside the runtime directory."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def quote(value):
    # Real LF bytes have the same meaning in both source-string profiles.
    return json.dumps(str(value), ensure_ascii=False).replace("\\n", "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/tptp-resource-tests")
    args = parser.parse_args()
    binary = Path(shutil.which(str(args.cetta)) or args.cetta).resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    foreign = output / "foreign"
    foreign.mkdir(exist_ok=True)
    # A non-executable local namesake must not shadow the PATH-selected binary.
    (foreign / binary.name).write_text("not an executable\n", encoding="utf-8")

    resource_checks = f"""
!(let* (($root (system:runtime-root))
        ($snapshot (str:concat $root (str:concat "/" (tptp:official-snapshot-path))))
        ($reader (tptp:open-reader strict))
        ($explicit (tptp:open-reader strict $snapshot))
        ($records (tptp:read-text $reader "thf(t,axiom,$true)."))
        ($printer (tptp:open-record-printer))
        ($explicit-printer (tptp:open-record-printer $root))
        ($printed (tptp:print-records-with $printer $records))
        ($explicit-printed (tptp:print-records-with $explicit-printer $records))
        ($closed (tptp:close-record-printer $printer))
        ($explicit-closed (tptp:close-record-printer $explicit-printer)))
   (ResourceChecks
     (== $root {quote(ROOT)})
     (== (system:cwd) {quote(foreign)})
     (== (tptp:read-text $reader "fof(a,axiom,p).")
         (tptp:read-text $explicit "fof(a,axiom,p)."))
     (== $printed $explicit-printed)
     (== $printed (str:concat "thf(t,axiom,$true)." (str:from-codepoints (10))))
     (== $records (tptp:read-text $reader $printed))
     (== (tptp:open-reader future-profile {quote(foreign / 'absent.tpp1')})
         (TPTP:UnknownReaderProfile future-profile))
     (case (tptp:open-reader strict {quote(foreign / 'absent.tpp1')})
       (((Error $operation $reason) True) ($other False)))))
"""
    # The shared source is HE; generated PeTTa spelling substitutes booleans.
    # These test expressions use each host's spelling explicitly.
    expressions = [
        '(== (tptp:szs-status (tptp:szs-output "% SZS status Theorem for x\n"))'
        ' (TPTP:SZSReported "Theorem"))',
        '(== (tptp:szs-status (tptp:szs-output "diagnostic only\n")) TPTP:SZSUnreported)',
        '(== (tptp:szs-pending (tptp:szs-output "% SZS output start Proof for x\np"))'
        ' (TPTP:SZSUnfinished "Proof" "p" 0))',
        '''(let $refs (tptp:input-references
              (tptp:read-text "fof(a,axiom,p,file('input',c_4))."))
            (let $first (car-atom $refs)
              (== (index-atom $first 3) c_4)))''',
    ]
    protocol_checks = "\n!(ProtocolChecks " + " ".join(expressions) + ")\n"
    print_checks = """
(= (resource-test:roundtrip $text)
   (let $records (tptp:read-text $text)
     (if (== (get-metatype $records) List)
       (let $printed (tptp:print-records $records)
         (== $records (tptp:read-text $printed)))
       False)))
!(PrintChecks
   (resource-test:roundtrip "thf(g,conjecture,((^[X:$o]:X) @ $true)).")
   (resource-test:roundtrip "thf(g,conjecture,?[Z:$o]:((^[X:$o]:X) @ Z)).")
   (resource-test:roundtrip "thf(g,conjecture,~((f @ x)!=x)).")
   (resource-test:roundtrip "fof(g,axiom,p,inference(copy,[status(thm)],[a,b])).")
   (resource-test:roundtrip "cnf(a,axiom,p|~q)."))
"""
    checks = 0
    for host in ("he", "petta@1.0.6"):
        dependency = (quote(ROOT / "lib/lib_tptp.metta") if host == "he"
                      else "(library lib_tptp)")
        source = (f"!(import! &self {dependency})\n" + resource_checks + protocol_checks + print_checks
                  + "\n!(__cetta_lib_system_runtime_root extra)\n")
        if host.startswith("petta"):
            source = source.replace(" True", " true").replace(" False", " false")
        entry = output / f"{host}-resources.metta"
        entry.write_text(source, encoding="utf-8")
        for mode in ("inline", "file", "path"):
            operands = ["-e", source] if mode == "inline" else [str(entry)]
            env = dict(os.environ)
            env["PATH"] = str(binary.parent) + os.pathsep + env.get("PATH", "")
            executable = binary.name if mode == "path" else str(binary)
            result = subprocess.run([executable, "--lang", host] + operands,
                                    cwd=foreign, env=env, text=True,
                                    capture_output=True, check=False)
            if result.returncode:
                raise AssertionError((host, mode, result.stderr or result.stdout))
            for label, count in (("ResourceChecks", 8), ("ProtocolChecks", 4), ("PrintChecks", 5)):
                found = re.findall(r"\(" + label + r" ([^()]*)\)", result.stdout)
                expected = ["true"] * count
                if len(found) != 1 or found[0].lower().split() != expected:
                    raise AssertionError((host, mode, label, result.stdout, result.stderr))
                checks += count
            if "(Error (__cetta_lib_system_runtime_root extra)" not in result.stdout:
                raise AssertionError((host, mode, "expected arity fault", result.stdout))
            checks += 1
    print("TPTPResources", checks, 0)


if __name__ == "__main__":
    main()

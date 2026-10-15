#!/usr/bin/env python3
"""Generate private diagnostic translation units; never rewrite src/."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("part", choices=["machine", "prepared"])
parser.add_argument("output", type=Path)
args = parser.parse_args()
source = Path("src/petta_search_machine.c" if args.part == "machine"
              else "src/prepared_pure_machine.c").read_text()
if args.part == "machine":
    anchor = "static bool petta_machine_maybe_collect(\n"
    assert source.count(anchor) == 1
    source = source.replace(anchor, '#include "machine_shadow.inc.c"\n\n' + anchor)
    start = """static bool petta_machine_maybe_collect(
    PettaMachineImpl *machine) {
    if (!machine)
        return false;
"""
    assert source.count(start) == 1
    source = source.replace(start, start + "    gc_probe_machine_boundary(machine);\n")
    signature = "PettaMachineStep petta_machine_next(\n"
    assert source.count(signature) == 1
    source = source.replace(signature, "static PettaMachineStep gc_probe_next_impl(\n")
    source += """
PettaMachineStep petta_machine_next(PettaMachine *machine, Atom **answer,
                                   Bindings *environment) {
    gc_probe_guest_depth++;
    PettaMachineStep result = gc_probe_next_impl(machine, answer, environment);
    gc_probe_guest_depth--;
    return result;
}
"""
else:
    source += '\n#include "prepared_shadow.inc.c"\n'
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(source)

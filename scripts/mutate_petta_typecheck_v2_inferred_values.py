#!/usr/bin/env python3
"""Create destructive mutations for inferred first-class value evidence."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = {
    "drop-singleton": (
        """    if (found) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_VALUE_CANDIDATE_SINGLETON);
    }
    return found;
""",
        """    if (found) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_VALUE_CANDIDATE_SINGLETON);
    }
    return NULL;
""",
    ),
    "first-ambiguous": (
        """        if (!atom_alpha_eq(found, declaration->type)) {
            if (ambiguous_out)
                *ambiguous_out = true;
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_VALUE_CANDIDATE_AMBIGUOUS);
            return NULL;
        }
""",
        """        if (!atom_alpha_eq(found, declaration->type)) {
            return found;
        }
""",
    ),
    "promote-inferred": (
        """        if (declaration->inferred) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_INFERRED_VALUE_CANDIDATE_IGNORED);
            continue;
        }
""",
        """        if (declaration->inferred) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_INFERRED_VALUE_CANDIDATE_IGNORED);
        }
""",
    ),
    "preserve-unrecognized-output": (
        """    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_OUTPUT_SHAPE_WIDENS_UNKNOWN);
    return atom_symbol(&check->scratch, "%Undefined%");
""",
        """    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_OUTPUT_SHAPE_WIDENS_UNKNOWN);
    return type;
""",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    old, new = MUTATIONS[arguments.mutation]
    if source.count(old) != 1:
        parser.error(
            f"mutation anchor count changed for {arguments.mutation}"
        )
    arguments.output.write_text(source.replace(old, new, 1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

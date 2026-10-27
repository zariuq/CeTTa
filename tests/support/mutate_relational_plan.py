#!/usr/bin/env python3
"""Create isolated indexed-count, encoding, validity and stored-variable defects."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
mutations = {
    "multiplicity": (
        "__builtin_mul_overflow(weight, (uint64_t)(ranges[f].end - ranges[f].begin), &weight)",
        "__builtin_mul_overflow(weight, (uint64_t)1u, &weight)"),
    "range": (
        "if (__builtin_mul_overflow(weight, (uint64_t)(ranges[f].end - ranges[f].begin), &weight))",
        "if ((weight *= (uint64_t)(ranges[f].end - ranges[f].begin), false))"),
    "count-overflow": ("__builtin_add_overflow(*total, weight, &next)",
                       "((next = *total + weight), false)"),
    "revision": ("plan->info.space_revision == space_revision(space)", "true"),
    "generation": ("plan->info.universe_epoch == space->native.universe->storage_epoch", "true"),
    "private-alias": ("previous < c; previous++", "false && previous < c; previous++"),
    "sharing": ("if (factors > 1u)", "if (factors > 2u)"),
    "encoding": ("header->subtag == GV_STRING));", "header->subtag == GV_STRING || header->subtag == GV_FLOAT));"),
    "whole-variable": ("if (kind == ATOM_VAR)", "if (false)"),
    "open-order": ("before[others_len] = j < i || (ground_only && ground_only[j]);",
                   "before[others_len] = ground_only && ground_only[j];"),
    "open-unbound": ("ready = part->sources[v] != SIZE_MAX;", "ready = true;"),
    "seed": ("bindings_apply_if_vars(seed, scratch, patterns[i])", "bindings_apply_if_vars(NULL, scratch, patterns[i])"),
    "dense-seed": ("bindings_apply_if_vars(seed, scratch, patterns[p])", "bindings_apply_if_vars(NULL, scratch, patterns[p])"),
    "seek": ("value < key || (upper && value == key)", "value <= key || (upper && value == key)"),
    "seek-cost": ("work->key_comparisons++;", "work->key_comparisons += 0u;"),
    "seek-rotation": (
        "level->current = (level->current + 1u) % level->count;\n    goto intersect;\nnarrow_range:",
        "level->current = (level->current + 0u) % level->count;\n    goto intersect;\nnarrow_range:"),
    "advance-rotation": (
        "level->current = (level->current + 1u) % level->count;\n    goto intersect;\nbacktrack:",
        "level->current = (level->current + 0u) % level->count;\n    goto intersect;\nbacktrack:"),
    "intersection": ("if (value == maximum)", "if (value <= maximum)"),
    "resume": ("cursor->key = key;", "cursor->key = 0u;"),
    "restore": ("!cetta_relational_plan_valid_for(cursor->plan, endpoint->space))\n        return CETTA_CONTINUATION_INVALIDATED;\n    cetta_relational_count_cursor_free(*endpoint->cursor);",
                "false)\n        return CETTA_CONTINUATION_INVALIDATED;\n    cetta_relational_count_cursor_free(*endpoint->cursor);"),
    "plan-retention": ("!cetta_relational_plan_retain(plan)", "false"),
    "incidence": ("size_t slot = plan->variable_offsets[variable];",
                  "size_t slot = plan->variable_offsets[variable] + 1u;"),
    "trail-restore": ("ranges[current[i].factor] = current[i].before;",
                      "ranges[current[i].factor] = ranges[current[i].factor];"),
    "trail-bound": ("it->position, it->before.end, maximum, false, &work)",
                    "it->before.end, it->before.end, maximum, false, &work)"),
}
old, new = mutations[kind]
assert source.count(old) == 1, (kind, source.count(old))
Path(output_path).write_text(source.replace(old, new))

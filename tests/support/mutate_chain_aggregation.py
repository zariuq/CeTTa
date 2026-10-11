#!/usr/bin/env python3
"""Create isolated path-message and caller-admission defects."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
mutations = {
    "count": (".count = count, .sum = sum,", ".count = 1, .sum = sum,"),
    "correlation": ("if (other != left + 1u && other + 1u != left)",
                    "if (false && other != left + 1u && other + 1u != left)"),
    "sum": ("__builtin_mul_overflow(count, (__int128)value,",
            "__builtin_mul_overflow((__int128)1, (__int128)value,"),
    "seed": ("bindings_apply_if_vars(seed, scratch, patterns[index])",
             "bindings_apply_if_vars(NULL, scratch, patterns[index])"),
    "count-range": ("count >= 0 && count <= (__int128)UINT64_MAX;",
                    "count >= 0;"),
}
old, new = mutations[kind]
assert source.count(old) == 1, (kind, source.count(old))
Path(output_path).write_text(source.replace(old, new))

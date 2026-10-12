"""Build-time symbolic adapter to the existing native predictive analyzer."""

import json
from pathlib import Path
import subprocess

DEFAULT_COMPILER = Path(__file__).resolve().parents[1] / "runtime/analyze-parser-predictive-v1"


class PredictiveConflict(ValueError):
    pass


def analyze(rows, start, compiler=DEFAULT_COMPILER):
    """Preserve row occurrences while adapting symbolic token identities."""
    categories = sorted({row["category"] for row in rows})
    terminals = sorted({name for row in rows for kind, name in row["rhs"] if kind == "t"})
    cat_ids = {name: index for index, name in enumerate(categories)}
    token_ids = {name: index for index, name in enumerate(terminals)}
    if start not in cat_ids:
        raise ValueError("start category is not defined")
    fields = [f"{cat_ids[start]} {len(rows)}"]
    for row in rows:
        fields.append(f'{cat_ids[row["category"]]} {len(row["rhs"])}')
        for kind, name in row["rhs"]:
            if kind == "t":
                fields.append(f"0 {token_ids[name]} 0")
            elif kind == "n" and name in cat_ids:
                fields.append(f"1 {cat_ids[name]} 0")
            else:
                raise ValueError("unknown or unbound native grammar symbol")
    result = subprocess.run([str(Path(compiler).resolve())], input="\n".join(fields) + "\n",
                            text=True, capture_output=True, check=True)
    program = json.loads(result.stdout)
    if program.get("format") != "NativePredictiveAnalysisV1":
        raise ValueError("incompatible native predictive analysis")
    if program["status"] == 1:
        raise PredictiveConflict(f'FIRST/FOLLOW conflict: {categories[program["conflict_nonterminal"]]}: '
                                 f'occurrences {program["conflict_productions"]}')
    if program["status"] == 2:
        raise PredictiveConflict(f'zero-progress cycle: occurrences {program["cycle_productions"]}')
    if program["status"] != 0:
        raise ValueError("unknown native predictive status")
    first, follow, selected = {}, {}, {}
    width = len(program["terminals"]) + 1
    for index, nt in enumerate(program["nonterminals"]):
        category = categories[nt]
        first[category] = {"$EOF" if t is None else terminals[t] for t in program["first"][index]}
        follow[category] = {"$EOF" if t is None else terminals[t] for t in program["follow"][index]}
        for t in range(width):
            occurrence = program["decisions"][index * width + t]
            if occurrence is not None:
                token = "$EOF" if t == width - 1 else terminals[program["terminals"][t]]
                selected[category, token] = occurrence
    return first, follow, selected

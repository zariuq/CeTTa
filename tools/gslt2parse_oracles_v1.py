"""CeTTa's own gslt2parse Prolog oracles.

The oracles that reach no PeTTa source run with swipl straight from this
tree; an external PeTTa checkout is needed only for the PeTTa authorities
(the PeTTa parser and the document splitter that reads with it)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORACLES = ROOT / "experiments" / "gslt2parse_foundation" / "prolog"

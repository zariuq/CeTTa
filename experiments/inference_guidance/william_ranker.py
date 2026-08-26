"""Goal-sensitive WILLIAM ranking for the authored Prime proof frontier."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any

from hyperon.atoms import CettaAtom, OperationAtom
from hyperon.ext import register_atoms

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from gen_semiring_fixtures import SExpr, parse_forms, sexpr


class WilliamRankerError(RuntimeError):
    """A malformed request or unavailable explicitly configured model."""


_MODEL: Any | None = None
_MODEL_IDENTITY: tuple[str, str] | None = None
_MODEL_DIGEST: str | None = None
_RANK_CACHE: dict[tuple[str, str, str], str] = {}

_FORMULA_SYMBOLS = {
    "smm:iff": "↔",
    "smm:imp": "→",
    "smm:not": "¬",
}

_VARIABLE_SYMBOLS = {
    "ph": "𝜑",
    "ps": "𝜓",
    "ch": "𝜒",
    "th": "𝜃",
    "ta": "𝜏",
    "et": "𝜂",
    "ze": "𝜁",
}


def _configured_model() -> tuple[Any, str]:
    global _MODEL, _MODEL_DIGEST, _MODEL_IDENTITY

    repository_value = os.environ.get("INFCONTROL_REPO")
    model_value = os.environ.get("WILLIAM_MODEL_DIR")
    if not repository_value or not model_value:
        raise WilliamRankerError(
            "INFCONTROL_REPO and WILLIAM_MODEL_DIR must be set explicitly"
        )

    repository = Path(repository_value).resolve()
    model_directory = Path(model_value).resolve()
    identity = (str(repository), str(model_directory))
    if (
        _MODEL is not None
        and _MODEL_DIGEST is not None
        and _MODEL_IDENTITY == identity
    ):
        return _MODEL, _MODEL_DIGEST

    sys.path.insert(0, str(repository.parent))
    from infcontrol.model import PredictionModel
    from infcontrol.protocol import model_sha256

    _MODEL = PredictionModel.load(model_directory)
    _MODEL_IDENTITY = identity
    _MODEL_DIGEST = model_sha256(model_directory)
    _RANK_CACHE.clear()
    return _MODEL, _MODEL_DIGEST


def _parse_one(atom: CettaAtom, field: str) -> SExpr:
    forms = parse_forms(str(atom))
    if len(forms) != 1:
        raise WilliamRankerError(f"{field} must contain exactly one expression")
    return forms[0]


def _variable_symbol(symbol: str) -> str | None:
    raw = symbol[1:] if symbol.startswith("$") else symbol
    raw = raw.split("#", 1)[0]
    raw = raw.removeprefix("smm:")
    return _VARIABLE_SYMBOLS.get(raw)


def _formula(node: SExpr) -> SExpr:
    if isinstance(node, list):
        return [_formula(item) for item in node]
    variable = _variable_symbol(node) if isinstance(node, str) else None
    if variable is not None:
        return variable
    return _FORMULA_SYMBOLS.get(node, node) if isinstance(node, str) else node


def _holds_formula(node: SExpr) -> SExpr:
    if (
        isinstance(node, list)
        and len(node) == 3
        and node[0] == "app"
        and node[1] == "smm:holds"
    ):
        return _formula(node[2])
    if (
        isinstance(node, list)
        and len(node) == 2
        and node[0] == "smm:holds"
    ):
        return _formula(node[1])
    raise WilliamRankerError(
        f"expected an smm:holds proposition, received {sexpr(node)}"
    )


def _candidate_formula(declaration: SExpr) -> SExpr:
    if (
        isinstance(declaration, list)
        and len(declaration) == 2
        and declaration[0] == "proof:fresh-declaration"
    ):
        declaration = declaration[1]
    if (
        not isinstance(declaration, list)
        or len(declaration) != 3
        or declaration[0] != ":"
    ):
        raise WilliamRankerError(
            f"expected a typing declaration, received {sexpr(declaration)}"
        )
    declaration_type = declaration[2]
    if (
        isinstance(declaration_type, list)
        and len(declaration_type) >= 3
        and declaration_type[0] == "->"
    ):
        return ["->", *(_holds_formula(item) for item in declaration_type[1:])]
    return _holds_formula(declaration_type)


def _rank_declarations(goal_atom: CettaAtom, declarations_atom: CettaAtom) -> list[CettaAtom]:
    model, model_digest = _configured_model()
    goal = _holds_formula(_parse_one(goal_atom, "goal"))
    declarations = _parse_one(declarations_atom, "declarations")
    if not isinstance(declarations, list):
        raise WilliamRankerError("declarations must be an expression list")

    goal_key = json.dumps(goal, ensure_ascii=False, separators=(",", ":"))
    declarations_key = sexpr(declarations)
    cache_key = (model_digest, goal_key, declarations_key)
    cached = _RANK_CACHE.get(cache_key)
    if cached is not None:
        return [CettaAtom(cached)]

    candidate_formulas = [_candidate_formula(item) for item in declarations]
    probabilities = model.predict(
        [goal] * len(candidate_formulas), candidate_formulas
    ).tolist()
    ranked = sorted(
        enumerate(zip(declarations, probabilities, strict=True)),
        key=lambda item: (-float(item[1][1]), item[0]),
    )
    ordered = [pair[0] for _position, pair in ranked]
    if len(ordered) != len(declarations):
        raise WilliamRankerError("ranking did not preserve declaration multiplicity")

    rendered = sexpr(ordered)
    _RANK_CACHE[cache_key] = rendered
    return [CettaAtom(rendered)]


@register_atoms
def expose_william_ranker() -> dict[str, OperationAtom]:
    return {
        "william:rank-declarations": OperationAtom(
            "william:rank-declarations", _rank_declarations, unwrap=False
        )
    }

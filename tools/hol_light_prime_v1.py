"""Emit an incremental Prime driver for the translated authored HOL checker.

Each exported event still runs through the source receiver and its claimed
conclusion checks. The driver retains the receiver's complete result between
top-level activations; a refused result remains refused. It does not implement
logical inference, trust source theorem names, or discard proof receipts.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

from hol_light_petta_v1 import library_text
from library_spelling import Symbol, read_forms, show
from petta_prime_guest_v1 import compile_source, form


def receive_program(events: list[str], binary: Path, intermediate: Path,
                    observation: object | None = None) -> tuple[str, dict]:
    """Translate the retained source receiver, then supply a Prime driver.

    The source API accepts a list of events and folds receive-from over it.
    This driver supplies singleton lists in that order, retaining the exact
    Some(state)/None result. Groundness is checked before each event, including
    when the earlier result has already refused. The target registry binding
    is private to this generated closed program; program mutation and concurrent
    publication are outside this driver contract.
    """
    identity = hashlib.sha256("\n".join(events).encode()).hexdigest()[:16]
    seed = "__prime_hol_seed_" + identity
    advance = "__prime_hol_advance_" + identity
    retained = Symbol("&__prime_hol_result_" + identity)
    result, event, state, refused, empty = map(Symbol,
        ("$result", "$event", "$state", "$refused", "$empty"))
    helpers = [
        form("=", form(seed), form("let", empty, form("quote", Symbol("HOL:Nil")),
                                   form("hol-light:standard-receive", empty))),
        form("=", form(advance, result, event),
             form("if", form("hol-light:ground", event),
                  form("case", result,
                       [[form("Some", state), form("hol-light:receive-from", state, event)],
                        [refused, Symbol("None")]]), Symbol("None"))),
    ]
    source = library_text() + "\n" + "\n".join(map(show, helpers)) + "\n"
    translated, inventory = compile_source(source, binary, intermediate, sort_values="closed-strings")
    queries = [(Symbol("!"), form("bind!", retained, form(seed)))]
    for text in events:
        parsed = read_forms(text)
        if len(parsed) != 1 or not isinstance(parsed[0], list):
            raise ValueError("an exported event must be a single passive expression")
        singleton = form("HOL:Cons", parsed[0], Symbol("HOL:Nil"))
        supplied = form("function", form("return", singleton))
        queries.append((Symbol("!"), form("bind!", retained,
                                         form(advance, retained, supplied))))
    if observation is None:
        observation = form("hol-light:accepted", retained)
    else:
        def replace(node: object) -> object:
            if node == Symbol("$received"):
                return retained
            if isinstance(node, list):
                return [replace(child) for child in node]
            return node
        observation = replace(observation)
    queries.append((Symbol("!"), observation))
    return translated + "\n" + "\n".join(map(show, queries)) + "\n", {
        **inventory, "receiver_driver": "ordered-singleton-events",
        "received_events": len(events), "registry_initializations": 1,
        "registry_updates": len(events),
    }

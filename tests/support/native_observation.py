"""Read the CLI's structural answer and host-service effect observations.

The protocol reports query frontiers. It does not assert answer/effect
interleaving inside an evaluator call, nor observe uninstrumented foreign I/O.
"""
from pathlib import Path
import json


class UnobservedValue(ValueError):
    pass


def check_value(tokens):
    required = 1
    for token in tokens:
        if required == 0 or not isinstance(token, list) or not token:
            raise ValueError("invalid structural token stream")
        required -= 1
        kind = token[0]
        if kind == "unobserved-grounded" or kind == "unobserved-variable":
            raise UnobservedValue("value needs an observation codec")
        if len(token) != 2:
            raise ValueError("invalid structural token")
        value = token[1]
        if kind == "expression":
            if type(value) is not int or value < 0:
                raise ValueError("invalid expression arity")
            required += value
        elif kind in ("symbol", "string", "big-integer", "rational"):
            if not isinstance(value, str):
                raise ValueError("invalid byte value")
            bytes.fromhex(value)
        elif kind == "variable":
            if type(value) is not int or value < 0:
                raise ValueError("invalid variable slot")
        elif kind == "integer":
            if not isinstance(value, str) or str(int(value)) != value:
                raise ValueError("invalid integer")
        elif kind == "float64":
            if not isinstance(value, str) or len(value) != 16:
                raise ValueError("invalid float bits")
            bytes.fromhex(value)
        elif kind == "boolean":
            if type(value) is not bool:
                raise ValueError("invalid boolean")
        elif kind == "internal-tag":
            if type(value) is not int:
                raise ValueError("invalid internal value tag")
        else:
            raise ValueError("unknown structural token")
    if required:
        raise ValueError("incomplete structural token stream")


def read_observation(path):
    queries, header, final, active = {}, None, None, 0
    outside_effects = []
    for line in Path(path).read_text().splitlines():
        event = json.loads(line)
        kind = event.get("event")
        if final is not None:
            raise ValueError("event after final observation")
        if header is None:
            if event != dict(event="header", version=1, answers="query-frontier",
                             effects="thread-host-services", values="cetta-structural-v1"):
                raise ValueError("unknown observation protocol")
            header = event
            continue
        if kind == "begin":
            active = event["query"] if not active else 0
            if not active or active in queries or active != len(queries) + 1:
                raise ValueError("invalid query opening")
            queries[active] = dict(answers=[], effects=[], foreign=[], terminal=None)
        elif kind in ("answer", "effect", "foreign", "end"):
            query = event["query"]
            if query != active or (not active and kind != "effect"):
                raise ValueError("event outside its query")
            if kind == "answer":
                if event.get("complete") is not True:
                    raise UnobservedValue("answer needs an observation codec")
                check_value(event["value"])
                queries[query]["answers"].append(event["value"])
            elif kind == "effect":
                value = (event["channel"], bytes.fromhex(event["bytes"]))
                (queries[query]["effects"] if active else outside_effects).append(value)
            elif kind == "foreign":
                queries[query]["foreign"].append(event["entering"])
            else:
                queries[query]["terminal"] = event
                active = 0
        elif kind == "final":
            final = event
        else:
            raise ValueError("unknown observation event")
    if header is None or final is None or active:
        raise ValueError("observation has no settled final receipt")
    declared, unsettled = final.get("queries_declared"), final.get("queries_unsettled")
    if (final.get("framing_ok") is not True or type(declared) is not int or
            type(unsettled) is not int or declared < len(queries) or
            unsettled < declared - len(queries) or
            (declared != len(queries) and not final.get("contract_exit"))):
        raise ValueError("observation catalogue/framing mismatch")
    if not final.get("values_complete"):
        raise UnobservedValue("document needs a value observation codec")
    if final.get("foreign_activity") or any(q["foreign"] for q in queries.values()):
        raise ValueError("foreign effects need a declared observation port")
    return dict(queries=queries, final=final, outside_effects=outside_effects)


def project(observation, spans):
    """Select original-query spans; administrative queries stay out of the
    guest's observations. Renumber selected variables together, preserving
    all sharing, value kinds, occurrence order and query boundaries.
    """
    variables, projected, selected = {}, [], set()
    for span in spans:
        if isinstance(span, dict):
            begin, end, status = span["begin"], span["end"], span["status"]
        else:
            (begin, end), status = span, None
        answers, effects = [], []
        for number in range(begin, end + 1):
            if number in selected:
                raise ValueError("overlapping original-query spans")
            selected.add(number)
            query = observation["queries"][number]
            terminal = query["terminal"]
            if terminal is None or terminal["stop"] != 0 or not terminal["output_ok"] or not terminal["cleanup_ok"]:
                raise ValueError("query frontier is unfinished or failed")
            if number not in (end, status) and query["answers"]:
                raise ValueError("administrative query returned guest-visible answers")
            for channel, data in query["effects"]:
                # Callback chunking is physical. Preserve the byte stream
                # and channel order, not an allocation-dependent chunk size.
                if effects and effects[-1][0] == channel:
                    effects[-1][1].append(data)
                else:
                    effects.append((channel, [data]))
            if number == end:
                for value in query["answers"]:
                    answers.append(tuple((kind, variables.setdefault(item, len(variables)))
                        if kind == "variable" else (kind, item) for kind, item in value))
        projected.append(dict(answers=answers,
                              effects=[(channel, b"".join(chunks)) for channel, chunks in effects]))
    if observation["outside_effects"]:
        raise ValueError("effects outside original-query spans need an observation mapping")
    for number, query in observation["queries"].items():
        if number not in selected and query["effects"]:
            raise ValueError("administrative effects need an observation mapping")
        if query["terminal"] is None or query["terminal"]["stop"] != 0:
            raise ValueError("unsettled administrative query")
    return projected

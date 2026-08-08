#!/usr/bin/env python3
"""Replay the Prime equation-call tournament from structured receipts."""

from __future__ import annotations

from collections import Counter, defaultdict
from copy import deepcopy
from pathlib import Path
import os
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/prime/need_equation_call_constitution.metta"
BINARY = Path(os.environ.get("CETTA_BIN", ROOT / "cetta"))


def tokens(text: str) -> list[str]:
    result: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch in "()":
            result.append(ch)
            i += 1
            continue
        if ch == '"':
            start = i
            i += 1
            escaped = False
            while i < len(text):
                current = text[i]
                i += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    break
            result.append(text[start:i])
            continue
        start = i
        while i < len(text) and not text[i].isspace() and text[i] not in "()":
            i += 1
        result.append(text[start:i])
    return result


def parse_one(text: str):
    stream = tokens(text)
    cursor = 0

    def parse():
        nonlocal cursor
        if cursor >= len(stream):
            raise ValueError("unexpected end of trace")
        token = stream[cursor]
        cursor += 1
        if token != "(":
            if token == ")":
                raise ValueError("unexpected closing parenthesis")
            return token
        items = []
        while cursor < len(stream) and stream[cursor] != ")":
            items.append(parse())
        if cursor >= len(stream):
            raise ValueError("unterminated trace expression")
        cursor += 1
        return items

    value = parse()
    if cursor != len(stream):
        raise ValueError("trailing trace tokens")
    return value


def render(value) -> str:
    if isinstance(value, list):
        return "(" + " ".join(render(item) for item in value) + ")"
    return value


class Answer:
    def __init__(self, form: int, occurrence: int, value, receipt):
        self.form = form
        self.occurrence = occurrence
        self.value = render(value)
        self.receipt = receipt
        self.events = receipt[2:] if receipt else []

    def events_named(self, name: str):
        return [event for event in self.events if event[2] == name]


def event_field(event, name: str):
    for item in event[3:]:
        if isinstance(item, list) and item and item[0] == name:
            if len(item) == 2:
                return render(item[1])
            return render(item[1:])
    return None


def event_ids(answer: Answer, name: str) -> set[str]:
    return {event[1] for event in answer.events_named(name)}


def field_set(answer: Answer, event_name: str, field: str) -> set[str]:
    return {
        value
        for event in answer.events_named(event_name)
        if (value := event_field(event, field)) is not None
    }


def value_bag(forms, form: int) -> Counter[str]:
    return Counter(answer.value for answer in forms[form])


def require(condition: bool, message: str):
    if not condition:
        raise AssertionError(message)


def validate_native_receipts(forms: dict[int, list[Answer]]):
    """Check the executable, occurrence-preserving receipt boundary.

    The native trace currently exports the closed event support, not the
    receipt DAG edges.  These checks therefore validate occurrence scope,
    payload stability, source/cell identity, and functional observations; they
    do not claim a C refinement theorem for causal reachability.
    """
    required = {
        "evaluate-cell": ("need-session", "cell", "before"),
        "observe-cell": ("need-session", "cell", "after"),
        "inspect-origin": ("need-session", "cell", "after"),
        "read-state": ("state", "after"),
        "write-state": ("state", "before", "after"),
        "use-equation": ("application", "rule", "before", "after"),
        "resample": ("before",),
    }
    for form, answers in forms.items():
        require(
            len({answer.occurrence for answer in answers}) == len(answers),
            f"form {form}: duplicate answer occurrence ID",
        )
        occurrence_payload = {}
        for answer in answers:
            require(
                isinstance(answer.receipt, list) and
                len(answer.receipt) >= 2 and
                answer.receipt[0] == "receipt",
                f"form {form}: malformed receipt envelope",
            )
            receipt_id = answer.receipt[1]
            ids = [event[1] for event in answer.events]
            require(
                len(ids) == len(set(ids)),
                f"form {form}: duplicate event ID inside one receipt",
            )
            cell_observations = {}
            origin_observations = {}
            state_writes = defaultdict(dict)
            for event in answer.events:
                require(
                    isinstance(event, list) and len(event) >= 3 and
                    event[0] == "event" and event[1].isdigit(),
                    f"form {form}: malformed receipt event",
                )
                event_name = event[2]
                require(
                    event_name in required,
                    f"form {form}: unknown receipt event {event_name}",
                )
                for field in required[event_name]:
                    require(
                        event_field(event, field) is not None,
                        f"form {form}: {event_name} lacks {field}",
                    )
                application = event_field(event, "application")
                argument = event_field(event, "argument")
                if event_name in {"evaluate-cell", "observe-cell"}:
                    require(
                        (application is None) == (argument is None),
                        f"form {form}: partial source-argument identity",
                    )

                scoped_id = (receipt_id, event[1])
                payload = render(event[2:])
                previous = occurrence_payload.get(scoped_id)
                require(
                    previous is None or previous == payload,
                    f"form {form}: one event ID names two payloads",
                )
                occurrence_payload[scoped_id] = payload

                if event_name == "observe-cell":
                    cell = (
                        event_field(event, "need-session"),
                        event_field(event, "cell"),
                    )
                    observation = event_field(event, "after")
                    previous = cell_observations.get(cell)
                    require(
                        previous is None or previous == observation,
                        f"form {form}: one Need cell has two observations",
                    )
                    cell_observations[cell] = observation
                elif event_name == "inspect-origin":
                    cell = (
                        event_field(event, "need-session"),
                        event_field(event, "cell"),
                    )
                    origin = event_field(event, "after")
                    previous = origin_observations.get(cell)
                    require(
                        previous is None or previous == origin,
                        f"form {form}: one Need cell has two origins",
                    )
                    origin_observations[cell] = origin
                elif event_name == "write-state":
                    state = event_field(event, "state")
                    before = event_field(event, "before")
                    after = event_field(event, "after")
                    previous = state_writes[state].get(before)
                    require(
                        previous is None or previous == after,
                        f"form {form}: conflicting sibling writes merged",
                    )
                    state_writes[state][before] = after


def mutation_is_killed(check, message: str):
    try:
        check()
    except AssertionError:
        return
    raise AssertionError(message)


def run_frontier(frontier: str | None):
    common = [str(BINARY), "--lang", "prime"]
    if frontier is not None:
        common.extend(["--prime-rewrite-frontier", frontier])
    common.append(str(SOURCE))
    plain = subprocess.run(common, text=True, capture_output=True, check=False)
    traced = subprocess.run(
        common[:-1] + ["--emit-prime-need-trace", str(SOURCE)],
        text=True,
        capture_output=True,
        check=False,
    )
    require(plain.returncode == 0, f"plain run failed: {plain.stderr}")
    require(traced.returncode == 0, f"traced run failed: {traced.stderr}")
    require(
        plain.stdout == traced.stdout,
        "enabling the receipt observer changed the answer stream",
    )

    forms: dict[int, list[Answer]] = defaultdict(list)
    completion: dict[int, str] = {}
    for line in traced.stderr.splitlines():
        if not line.startswith("(prime-need:"):
            continue
        node = parse_one(line)
        if node[0] == "prime-need:answer":
            form = int(node[1])
            forms[form].append(
                Answer(form, int(node[2]), node[3][1], node[4])
            )
        elif node[0] == "prime-need:completion":
            completion[int(node[1])] = node[2]
    validate_native_receipts(forms)
    return forms, completion, plain.stdout


def admitted(forms, form: int) -> list[Answer]:
    return [answer for answer in forms[form]
            if answer.events_named("use-equation")]


def admitted_bag(forms, form: int) -> Counter[str]:
    return Counter(answer.value for answer in admitted(forms, form))


def producer_count(forms, form: int) -> int:
    return len(set().union(*(event_ids(answer, "evaluate-cell")
                             for answer in forms[form])))


def main() -> int:
    forms, completion, default_stdout = run_frontier(None)
    explicit_monolithic, explicit_completion, explicit_stdout = run_frontier(
        "monolithic")
    require(default_stdout == explicit_stdout,
            "the explicit monolithic switch changed the default answer stream")
    require(completion == explicit_completion,
            "the explicit monolithic switch changed completion")
    require(
        [[answer.value for answer in explicit_monolithic[i]]
         for i in range(1, 30)] ==
        [[answer.value for answer in forms[i]] for i in range(1, 30)],
        "the explicit monolithic switch changed traced answers",
    )

    checks = []

    def case(name, action):
        try:
            action()
            checks.append((name, True, ""))
        except AssertionError as error:
            checks.append((name, False, str(error)))

    case(
        "01-unused-fault",
        lambda: (
            require(value_bag(forms, 1) == Counter({"c01-unused": 1}),
                    "unused fault did not return exactly once"),
            require(not forms[1][0].events_named("evaluate-cell"),
                    "unused argument was evaluated"),
            require(not forms[1][0].events_named("observe-cell"),
                    "unused argument was observed"),
            require(not forms[1][0].events_named("write-state"),
                    "unused argument produced an effect receipt"),
        ),
    )

    def case02():
        require(value_bag(forms, 2) == Counter({"c02-a": 1, "c02-b": 1}),
                "strict candidates lost an answer")
        require(len(set().union(*(event_ids(a, "evaluate-cell")
                                  for a in forms[2]))) == 1,
                "strict candidates did not share one producer event")
        require(len(set().union(*(field_set(a, "use-equation", "rule")
                                  for a in forms[2]))) == 2,
                "strict candidates lost rule-occurrence identity")

    case("02-strict-strict", case02)

    def case03():
        require(value_bag(forms, 3) == Counter({
            "(c03-strict A)": 1,
            "(c03-wild A)": 1,
            "(c03-wild B)": 1,
        }), "strict/wild answer bag is wrong")
        require(len(set().union(*(event_ids(a, "evaluate-cell")
                                  for a in forms[3]))) == 1,
                "strict/wild candidates repeated the producer")
        require(all(field_set(a, "observe-cell", "cell") == {"1"}
                    for a in forms[3]),
                "strict/wild answers did not retain one shared cell")

    case("03-strict-wild-used", case03)

    def case04():
        require(value_bag(forms, 4) == Counter({
            "c04-strict": 1, "c04-constant": 1,
        }), "constant wildcard multiplicity is wrong")
        constant = next(a for a in forms[4] if a.value == "c04-constant")
        require(not constant.events_named("evaluate-cell") and
                not constant.events_named("observe-cell"),
                "constant wildcard receipt contains irrelevant support")

    case("04-strict-wild-constant", case04)

    def case05():
        require(value_bag(forms, 5) == Counter({
            "c05-left": 1,
            "c05-right": 2,
            "(tournament:c05 X Y)": 1,
        }), "baseline disjoint-demand bag changed")
        left = next(a for a in forms[5] if a.value == "c05-left")
        require(field_set(left, "observe-cell", "argument") == {"1"},
                "left answer observed an irrelevant argument")
        for right in (a for a in forms[5] if a.value == "c05-right"):
            require(field_set(right, "observe-cell", "argument") == {"1", "2"},
                    "baseline right receipt no longer exposes frontier contamination")

    case("05-disjoint-supports", case05)

    def case06():
        require(value_bag(forms, 6) == Counter({
            "c06-right": 1,
            "c06-left": 2,
            "(tournament:c06 X Y)": 1,
        }), "reversed baseline bag changed")
        mapped_left_first = Counter({
            value.replace("c05", "c0x"): count
            for value, count in value_bag(forms, 5).items()
            if not value.startswith("(tournament:")
        })
        mapped_right_first = Counter({
            value.replace("c06", "c0x"): count
            for value, count in value_bag(forms, 6).items()
            if not value.startswith("(tournament:")
        })
        require(mapped_left_first != mapped_right_first,
                "baseline unexpectedly ceased to discriminate declaration order")

    case("06-declaration-reversal", case06)

    def case07():
        require(value_bag(forms, 7) == Counter({
            "c07-left": 1, "c07-right": 2, "c07-both": 1,
            "(tournament:c07 X Y)": 1,
        }), "overlapping-support baseline bag changed")
        both = next(a for a in forms[7] if a.value == "c07-both")
        require(field_set(both, "observe-cell", "argument") == {"1", "2"},
                "two-argument candidate lacks its exact support")

    case("07-overlapping-supports", case07)

    def case08():
        require(value_bag(forms, 8) == Counter({"c08-aa": 1, "c08-bb": 1}),
                "aliased explicit cell decorrelated")
        require(value_bag(forms, 9) == Counter({
            "c08-aa": 1, "c08-ab": 1, "c08-ba": 1, "c08-bb": 1,
        }), "two textual allocations did not form a cross-product")
        require(all(len(field_set(a, "observe-cell", "cell")) >= 2
                    for a in forms[9]),
                "independent textual cells collapsed to one identity")

    case("08-alias-vs-text", case08)

    def case09():
        require(value_bag(forms, 10) == Counter({"c09-aa": 1, "c09-bb": 1}),
                "shared context binding decorrelated")
        require(value_bag(forms, 11) == Counter({
            "c09-aa": 1, "c09-ab": 1, "c09-ba": 1, "c09-bb": 1,
        }), "independent identical-looking contexts collapsed")
        require(all(len(field_set(a, "observe-cell", "cell")) >= 2
                    for a in forms[11]),
                "independent context cells lost identity")

    case("09-context-identity", case09)

    def case10():
        require(value_bag(forms, 12) == Counter({"c10-duplicate": 2}),
                "duplicate admitted rules lost multiplicity")
        rules = set().union(*(field_set(a, "use-equation", "rule")
                              for a in forms[12]))
        require(len(rules) == 2,
                "duplicate structural equations collapsed occurrence identity")

    case("10-duplicate-equations", case10)

    def case11():
        require(value_bag(forms, 13) == Counter({
            "(c11-delay A A)": 1, "(c11-delay B B)": 1,
        }), "delay failed call-time correlation")
        require(value_bag(forms, 14) == Counter({
            "(c11-resample A A)": 1, "(c11-resample A B)": 1,
            "(c11-resample B A)": 1, "(c11-resample B B)": 1,
        }), "resample failed to form the fresh-event cross-product")
        require(len(set().union(*(event_ids(a, "evaluate-cell")
                                  for a in forms[13]))) == 1,
                "delay repeated its producer")
        require(all(len(event_ids(a, "resample")) == 2 for a in forms[14]),
                "each resample answer must contain two fresh events")

    case("11-delay-vs-resample", case11)

    def case12():
        require(value_bag(forms, 16) == Counter({
            "c12-before-a": 1, "c12-before-b": 1,
        }), "before-choice effect changed values")
        require(value_bag(forms, 19) == Counter({
            "c12-after-a": 1, "c12-after-b": 1,
        }), "after-choice effect changed values")
        before_writes = set().union(*(event_ids(a, "write-state")
                                      for a in forms[16]))
        after_writes = set().union(*(event_ids(a, "write-state")
                                     for a in forms[19]))
        require(len(before_writes) == 1,
                "one pre-choice effect was not shared causally")
        require(len(after_writes) == 2,
                "post-choice effects lost branch occurrence identity")
        require(value_bag(forms, 17) == Counter({"0": 1}) and
                value_bag(forms, 20) == Counter({"0": 1}),
                "branch receipts leaked into ambient state")

    case("12-transactional-effects", case12)

    def case13():
        require(value_bag(forms, 22) == Counter({
            "(c13-conflict 1)": 1, "(c13-conflict 2)": 1,
        }), "conflicting sibling outcomes changed")
        writes = [a.events_named("write-state")[0] for a in forms[22]]
        require({event_field(event, "state") for event in writes} == {"1"},
                "sibling writes do not identify one state cell")
        require({event_field(event, "after") for event in writes} == {"1", "2"},
                "sibling conflict lacks divergent observations")
        require(value_bag(forms, 23) == Counter({"0": 1}),
                "conflicting siblings committed ambient state")
        compatible = [a for a in forms[24] if a.value == "c13-compatible"]
        require(len(compatible) == 2 and
                len(set().union(*(event_ids(a, "observe-cell")
                                  for a in compatible))) == 1,
                "compatible duplicate rules do not share one observation")
        require(len(set().union(*(field_set(a, "use-equation", "rule")
                                  for a in compatible))) == 2,
                "compatible answers collapsed duplicate rule occurrences")

    case("13-conflict-vs-compatibility", case13)

    def case14():
        require(value_bag(forms, 27) == Counter({
            "(c14-roundtrip A A)": 1,
            "(c14-roundtrip B B)": 1,
        }), "persistence round-trip lost correlation")
        require(all(len(field_set(a, "observe-cell", "cell")) == 1
                    for a in forms[27]),
                "round-trip duplicated the persisted cell")

    case("14-persistence-roundtrip", case14)

    def case15():
        require(admitted_bag(forms, 28) == Counter({
            "c15-left": 1, "c15-right": 1, "c15-both": 1,
        }), "pre-forced overlap changed admitted occurrences")
        support = {
            answer.value: field_set(answer, "observe-cell", "argument")
            for answer in admitted(forms, 28)
        }
        require(support == {
            "c15-left": set(),
            "c15-right": {"2"},
            "c15-both": {"2"},
        }, "pre-forced overlap acquired excess support")
        require(producer_count(forms, 28) == 1,
                "baseline pre-forced overlap duplicated its pending producer")

    case("15-pre-forced-overlap", case15)

    def case16():
        require(admitted_bag(forms, 29) == Counter({
            "(c16-equal A)": 1, "(c16-equal B)": 1,
        }), "nonlinear equality admitted a mixed or missing occurrence")
        require(all(field_set(answer, "observe-cell", "argument") ==
                    {"1", "2"} for answer in admitted(forms, 29)),
                "nonlinear equality was refuted without both paths")
        require(value_bag(forms, 29) == Counter({
            "(c16-equal A)": 1,
            "(c16-equal B)": 1,
            "(tournament:c16 A B)": 1,
            "(tournament:c16 B A)": 1,
        }), "nonlinear equality residual family changed")

    case("16-nonlinear-equality-support", case16)

    require(set(completion) == set(range(1, 30)),
            "not every executed form reported completion")
    require(all(value == "complete" for value in completion.values()),
            "a tournament form was incomplete")

    policy_observations = {}

    def check_experimental_frontier(frontier: str,
                                    expected_case02_producers: int):
        observed, observed_completion, _ = run_frontier(frontier)
        policy_observations[frontier] = observed

        def add_case(name, action):
            case(f"{frontier}:{name}", action)

        add_case(
            "01-unused-fault",
            lambda: (
                require(admitted_bag(observed, 1) ==
                        Counter({"c01-unused": 1}),
                        "unused fault changed its admitted answer"),
                require(producer_count(observed, 1) == 0,
                        "unused faulting argument was evaluated"),
            ),
        )

        def experimental02():
            require(admitted_bag(observed, 2) ==
                    Counter({"c02-a": 1, "c02-b": 1}),
                    "strict/strict admitted answers changed")
            require(producer_count(observed, 2) ==
                    expected_case02_producers,
                    "strict/strict producer characterization changed")
            require(len(set().union(*(field_set(a, "use-equation", "rule")
                                      for a in admitted(observed, 2)))) == 2,
                    "strict/strict lost rule occurrence identity")

        add_case("02-strict-strict", experimental02)

        def experimental03():
            require(admitted_bag(observed, 3) == Counter({
                "(c03-strict A)": 1,
                "(c03-wild A)": 1,
                "(c03-wild B)": 1,
            }), "strict/wild admitted answers changed")
            require(producer_count(observed, 3) == 1,
                    "strict/wild repeated its producer")
            require(all(field_set(a, "observe-cell", "cell") == {"1"}
                        for a in admitted(observed, 3)),
                    "strict/wild lost source-cell sharing")

        add_case("03-strict-wild-used", experimental03)

        def experimental04():
            require(admitted_bag(observed, 4) == Counter({
                "c04-strict": 1, "c04-constant": 1,
            }), "strict/constant admitted answers changed")
            constant = next(a for a in admitted(observed, 4)
                            if a.value == "c04-constant")
            require(not constant.events_named("evaluate-cell") and
                    not constant.events_named("observe-cell"),
                    "constant answer inherited irrelevant support")

        add_case("04-strict-wild-constant", experimental04)

        def experimental05():
            require(admitted_bag(observed, 5) == Counter({
                "c05-left": 1, "c05-right": 1,
            }), "disjoint supports are not occurrence-exact")
            left = next(a for a in admitted(observed, 5)
                        if a.value == "c05-left")
            right = next(a for a in admitted(observed, 5)
                         if a.value == "c05-right")
            require(field_set(left, "observe-cell", "argument") == {"1"}
                    and field_set(right, "observe-cell", "argument") == {"2"},
                    "disjoint answers do not carry least support")
            require(producer_count(observed, 5) == 2,
                    "disjoint arguments were not each evaluated once")

        add_case("05-disjoint-supports", experimental05)

        def experimental06():
            require(admitted_bag(observed, 6) == Counter({
                "c06-left": 1, "c06-right": 1,
            }), "reversed disjoint supports are not occurrence-exact")
            left_first = Counter({
                value.replace("c05", "c0x"): count
                for value, count in admitted_bag(observed, 5).items()
            })
            right_first = Counter({
                value.replace("c06", "c0x"): count
                for value, count in admitted_bag(observed, 6).items()
            })
            require(left_first == right_first,
                    "candidate declaration order changed admitted answers")

        add_case("06-declaration-reversal", experimental06)

        def experimental07():
            require(admitted_bag(observed, 7) == Counter({
                "c07-left": 1, "c07-right": 1, "c07-both": 1,
            }), "overlapping supports changed admitted occurrences")
            support = {
                answer.value: field_set(answer, "observe-cell", "argument")
                for answer in admitted(observed, 7)
            }
            require(support == {
                "c07-left": {"1"},
                "c07-right": {"2"},
                "c07-both": {"1", "2"},
            }, "overlapping answers do not carry exact support")
            require(producer_count(observed, 7) == 4,
                    "overlapping-support red characterization changed")

        add_case("07-overlapping-supports", experimental07)

        add_case(
            "08-alias-vs-text",
            lambda: (
                require(admitted_bag(observed, 8) ==
                        Counter({"c08-aa": 1, "c08-bb": 1}),
                        "aliased explicit cell decorrelated"),
                require(admitted_bag(observed, 9) == Counter({
                    "c08-aa": 1, "c08-ab": 1,
                    "c08-ba": 1, "c08-bb": 1,
                }), "independent textual cells collapsed"),
            ),
        )
        add_case(
            "09-context-identity",
            lambda: (
                require(admitted_bag(observed, 10) ==
                        Counter({"c09-aa": 1, "c09-bb": 1}),
                        "shared context binding decorrelated"),
                require(admitted_bag(observed, 11) == Counter({
                    "c09-aa": 1, "c09-ab": 1,
                    "c09-ba": 1, "c09-bb": 1,
                }), "independent contexts collapsed"),
            ),
        )

        def experimental10():
            require(admitted_bag(observed, 12) ==
                    Counter({"c10-duplicate": 2}),
                    "duplicate equations lost occurrence multiplicity")
            rules = set().union(*(field_set(a, "use-equation", "rule")
                                  for a in admitted(observed, 12)))
            require(len(rules) == 2,
                    "duplicate equations lost distinct rule identities")

        add_case("10-duplicate-equations", experimental10)

        add_case(
            "11-delay-vs-resample",
            lambda: (
                require(value_bag(observed, 13) == Counter({
                    "(c11-delay A A)": 1, "(c11-delay B B)": 1,
                }), "delay lost call-time correlation"),
                require(value_bag(observed, 14) == Counter({
                    "(c11-resample A A)": 1,
                    "(c11-resample A B)": 1,
                    "(c11-resample B A)": 1,
                    "(c11-resample B B)": 1,
                }), "resample lost freshness"),
            ),
        )

        def experimental12():
            require(admitted_bag(observed, 16) == Counter({
                "c12-before-a": 1, "c12-before-b": 1,
            }) and admitted_bag(observed, 19) == Counter({
                "c12-after-a": 1, "c12-after-b": 1,
            }), "transactional effect values changed")
            require(value_bag(observed, 17) == Counter({"0": 1}) and
                    value_bag(observed, 20) == Counter({"0": 1}),
                    "branch effects leaked into ambient state")

        add_case("12-transactional-effects", experimental12)

        def experimental13():
            require(admitted_bag(observed, 22) == Counter({
                "(c13-conflict 1)": 1, "(c13-conflict 2)": 1,
            }), "conflicting sibling observations changed")
            require(value_bag(observed, 23) == Counter({"0": 1}),
                    "conflicting siblings committed ambient state")
            require(admitted_bag(observed, 24) ==
                    Counter({"c13-compatible": 2}),
                    "compatible duplicate rules lost occurrences")

        add_case("13-conflict-vs-compatibility", experimental13)

        add_case(
            "14-persistence-roundtrip",
            lambda: (
                require(value_bag(observed, 27) == Counter({
                    "(c14-roundtrip A A)": 1,
                    "(c14-roundtrip B B)": 1,
                }), "persistence round-trip lost correlation"),
                require(all(len(field_set(a, "observe-cell", "cell")) == 1
                            for a in admitted(observed, 27)),
                        "persistence round-trip duplicated a cell"),
                require(set(observed_completion) == set(range(1, 30)) and
                        all(v == "complete"
                            for v in observed_completion.values()),
                        "experimental frontier changed completion"),
            ),
        )

        def experimental15():
            require(admitted_bag(observed, 28) == Counter({
                "c15-left": 1, "c15-right": 1, "c15-both": 1,
            }), "pre-forced overlap changed admitted occurrences")
            support = {
                answer.value: field_set(answer, "observe-cell", "argument")
                for answer in admitted(observed, 28)
            }
            require(support == {
                "c15-left": set(),
                "c15-right": {"2"},
                "c15-both": {"2"},
            }, "pre-forced overlap acquired excess support")
            require(producer_count(observed, 28) == 2,
                    "pre-forced overlap red producer characterization changed")

        add_case("15-pre-forced-overlap", experimental15)

        def experimental16():
            require(admitted_bag(observed, 29) == Counter({
                "(c16-equal A)": 1, "(c16-equal B)": 1,
            }), "nonlinear equality admitted a mixed or missing occurrence")
            require(all(field_set(answer, "observe-cell", "argument") ==
                        {"1", "2"} for answer in admitted(observed, 29)),
                    "nonlinear equality was refuted without both paths")
            require(value_bag(observed, 29) == Counter({
                "(c16-equal A)": 1,
                "(c16-equal B)": 1,
                "(tournament:c16 A B)": 1,
                "(tournament:c16 B A)": 1,
            }), "nonlinear equality residual family changed")

        add_case("16-nonlinear-equality-support", experimental16)

    check_experimental_frontier("candidate-local", 2)
    check_experimental_frontier("demand-cohort", 1)

    mutation_checks = []

    def mutation(name, setup, discriminator):
        try:
            mutated = deepcopy(policy_observations["demand-cohort"])
            setup(mutated)
            mutation_is_killed(
                lambda: discriminator(mutated),
                f"{name} mutation survived its discriminator",
            )
            mutation_checks.append((name, True, ""))
        except AssertionError as error:
            mutation_checks.append((name, False, str(error)))

    def duplicate_producer_setup(mutated):
        source = deepcopy(admitted(mutated, 2)[0]
                          .events_named("evaluate-cell")[0])
        source[1] = "900001"
        admitted(mutated, 2)[1].events.append(source)

    mutation(
        "duplicated-producer",
        duplicate_producer_setup,
        lambda mutated: require(
            producer_count(mutated, 2) == 1,
            "one source occurrence acquired two producer events",
        ),
    )

    def irrelevant_receipt_setup(mutated):
        event = deepcopy(admitted(mutated, 4)[0]
                         .events_named("observe-cell")[0])
        event[1] = "900002"
        constant = next(answer for answer in admitted(mutated, 4)
                        if answer.value == "c04-constant")
        constant.events.append(event)

    mutation(
        "irrelevant-argument-receipt",
        irrelevant_receipt_setup,
        lambda mutated: require(
            not next(answer for answer in admitted(mutated, 4)
                     if answer.value == "c04-constant")
                .events_named("observe-cell"),
            "constant answer inherited an irrelevant observation",
        ),
    )

    def collapse_rule_setup(mutated):
        duplicate_answers = admitted(mutated, 12)
        mutated[12].remove(duplicate_answers[1])

    mutation(
        "collapsed-duplicate-rule",
        collapse_rule_setup,
        lambda mutated: require(
            admitted_bag(mutated, 12) == Counter({"c10-duplicate": 2}) and
            len(set().union(*(field_set(answer, "use-equation", "rule")
                              for answer in admitted(mutated, 12)))) == 2,
            "duplicate admitted occurrence was collapsed",
        ),
    )

    def leak_effect_setup(mutated):
        mutated[17][0].value = "1"

    mutation(
        "leaked-losing-branch-effect",
        leak_effect_setup,
        lambda mutated: require(
            value_bag(mutated, 17) == Counter({"0": 1}),
            "a branch-local effect changed ambient state",
        ),
    )

    def merge_conflict_setup(mutated):
        event = deepcopy(mutated[22][1].events_named("write-state")[0])
        event[1] = "900005"
        mutated[22][0].events.append(event)

    mutation(
        "merged-conflicting-siblings",
        merge_conflict_setup,
        validate_native_receipts,
    )

    def order_sensitive_setup(mutated):
        extra = deepcopy(next(answer for answer in admitted(mutated, 6)
                              if answer.value == "c06-left"))
        extra.occurrence = 900006
        mutated[6].append(extra)

    def order_invariance_discriminator(mutated):
        left_first = Counter({
            value.replace("c05", "c0x"): count
            for value, count in admitted_bag(mutated, 5).items()
        })
        right_first = Counter({
            value.replace("c06", "c0x"): count
            for value, count in admitted_bag(mutated, 6).items()
        })
        require(left_first == right_first,
                "declaration reversal changed answer multiplicity")

    mutation(
        "order-sensitive-answer-bag",
        order_sensitive_setup,
        order_invariance_discriminator,
    )

    def preforced_excess_support_setup(mutated):
        left = next(answer for answer in admitted(mutated, 28)
                    if answer.value == "c15-left")
        right = next(answer for answer in admitted(mutated, 28)
                     if answer.value == "c15-right")
        event = deepcopy(right.events_named("observe-cell")[0])
        event[1] = "900007"
        left.events.append(event)

    mutation(
        "pre-forced-excess-support",
        preforced_excess_support_setup,
        lambda mutated: require(
            not next(answer for answer in admitted(mutated, 28)
                     if answer.value == "c15-left")
                .events_named("observe-cell"),
            "known argument answer acquired a suspended dependency",
        ),
    )

    def nonlinear_missing_support_setup(mutated):
        answer = admitted(mutated, 29)[0]
        answer.events = [
            event for event in answer.events
            if not (event[2] == "observe-cell" and
                    event_field(event, "argument") == "2")
        ]

    mutation(
        "nonlinear-missing-support",
        nonlinear_missing_support_setup,
        lambda mutated: require(
            all(field_set(answer, "observe-cell", "argument") ==
                {"1", "2"} for answer in admitted(mutated, 29)),
            "nonlinear equality was accepted without both observations",
        ),
    )

    for name, passed, detail in checks:
        print(f"{'PASS' if passed else 'FAIL'} {name}"
              + (f": {detail}" if detail else ""))
    passed = sum(1 for _, ok, _ in checks if ok)
    failed = len(checks) - passed
    print(
        f"(PrimeNeedEquationCallTournamentSummary "
        f"{len(checks)} {passed} {failed} "
        f"frontiers 3 order-invariance monolithic-red "
        f"candidate-local-green demand-cohort-green)"
    )
    print(
        "(PrimeNeedRewriteFrontierEvidence "
        "candidate-local same-support-sharing red residual-precision red "
        "demand-cohort same-support-sharing green "
        "overlapping-producer-reuse red residual-precision red)"
    )
    for name, passed_mutation, detail in mutation_checks:
        print(f"{'PASS' if passed_mutation else 'FAIL'} mutation:{name}"
              + (f": {detail}" if detail else ""))
    killed = sum(1 for _, ok, _ in mutation_checks if ok)
    surviving = len(mutation_checks) - killed
    print(
        f"(PrimeNativeReceiptMutationSummary "
        f"{len(mutation_checks)} {killed} {surviving})"
    )
    return 0 if failed == 0 and surviving == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ValueError) as error:
        print(f"FAIL tournament infrastructure: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Check that Prime MIL route qualification rejects hidden HE assistance."""

from __future__ import annotations

import importlib.util
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "bench_prime_mil", REPO / "scripts" / "bench_prime_mil.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load Prime MIL benchmark module")
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


def route_lines(
    legacy_he_applicability: int,
    map_rel_realized: int = 0,
) -> list[str]:
    values = {
        "prime-checking-route-scoped-regular": 0,
        "prime-checking-route-authored-regular": 1,
        "prime-checking-route-declared-regular": 0,
        "prime-checking-route-closed-regular": 0,
        "prime-checking-route-ambient-formation": 0,
        "prime-legacy-he-checking": 0,
        "prime-legacy-he-typed-applicability": legacy_he_applicability,
        "prime-level-normalization-step": 7,
        "prime-declaration-polymorphic-lookup": 2,
        "prime-declaration-level-parameter-fresh": 5,
        "prime-declaration-level-instance": 3,
        "prime-declaration-level-constraint": 1,
        "prime-regular-kernel-conversion-interior-check": 0,
        "prime-regular-kernel-synthesis-interior-check": 0,
        "prime-regular-kernel-checking-interior-check": 0,
        "prime-native-calculus-candidate": 4,
        "prime-native-map-realized": 0,
        "prime-native-hyp-realized": 3,
        "prime-native-calculus-declined": 1,
        "prime-native-calculus-fault": 0,
        "prime-native-hyp-admission-cache-hit": 2,
        "prime-native-hyp-admission-cache-miss": 1,
        "prime-native-hyp-denotation-admitted": 1,
        "prime-native-hyp-denotation-fallback": 0,
        "prime-native-hyp-candidate-bag-realized": 2,
        "prime-native-hyp-finite-provider-admitted": 1,
        "prime-native-hyp-finite-provider-fallback": 0,
        "prime-native-hyp-finite-search-realized": 2,
        "prime-native-map-rel-realized": map_rel_realized,
    }
    return [f"runtime-counter {name} {value}" for name, value in values.items()]


native = BENCH.parse_checking_routes(route_lines(0))
if native["qualification"] != "prime-native-regular":
    raise RuntimeError("unassisted native route was not qualified as native")
if native["declaration_work"] != {
        "prime-level-normalization-step": 7,
        "prime-declaration-polymorphic-lookup": 2,
        "prime-declaration-level-parameter-fresh": 5,
        "prime-declaration-level-instance": 3,
        "prime-declaration-level-constraint": 1,
}:
    raise RuntimeError("declaration-instantiation work was not retained")
if native["native_calculus"] != {
    "prime-native-calculus-candidate": 4,
    "prime-native-map-realized": 0,
    "prime-native-hyp-realized": 3,
    "prime-native-calculus-declined": 1,
    "prime-native-calculus-fault": 0,
    "prime-native-hyp-admission-cache-hit": 2,
    "prime-native-hyp-admission-cache-miss": 1,
    "prime-native-hyp-denotation-admitted": 1,
    "prime-native-hyp-denotation-fallback": 0,
    "prime-native-hyp-candidate-bag-realized": 2,
    "prime-native-hyp-finite-provider-admitted": 1,
    "prime-native-hyp-finite-provider-fallback": 0,
    "prime-native-hyp-finite-search-realized": 2,
    "prime-native-map-rel-realized": 0,
}:
    raise RuntimeError("native-calculus realization work was not retained")
if BENCH.native_calculus_qualification_error(
    "native-grandparent",
    native["native_calculus"],
    native["interior_checks"],
) is not None:
    raise RuntimeError("exact native hyp denotation was not qualified")

native_list = BENCH.parse_checking_routes(route_lines(0, 1))
if BENCH.native_calculus_qualification_error(
    "native-list-map-rel",
    native_list["native_calculus"],
    native_list["interior_checks"],
) is not None:
    raise RuntimeError("exact native List lifting was not qualified")
if BENCH.native_calculus_qualification_error(
    "native-list-map-rel",
    native["native_calculus"],
    native["interior_checks"],
) is None:
    raise RuntimeError("missing native List lifting retained a native claim")

fallback = dict(native["native_calculus"])
fallback["prime-native-hyp-denotation-admitted"] = 0
fallback["prime-native-hyp-denotation-fallback"] = 1
if BENCH.native_calculus_qualification_error(
    "native-grandparent", fallback, native["interior_checks"]
) is None:
    raise RuntimeError("semantic fallback retained a native hyp claim")

missing_candidate_bag = dict(native["native_calculus"])
missing_candidate_bag["prime-native-hyp-candidate-bag-realized"] = 0
if BENCH.native_calculus_qualification_error(
    "native-grandparent",
    missing_candidate_bag,
    native["interior_checks"],
) is None:
    raise RuntimeError("relational candidate production was absent from a native claim")

missing_finite_provider = dict(native["native_calculus"])
missing_finite_provider["prime-native-hyp-finite-provider-admitted"] = 0
if BENCH.native_calculus_qualification_error(
    "native-grandparent",
    missing_finite_provider,
    native["interior_checks"],
) is None:
    raise RuntimeError("finite evidence coverage was absent from a native claim")

missing_finite_search = dict(native["native_calculus"])
missing_finite_search["prime-native-hyp-finite-search-realized"] = 0
if BENCH.native_calculus_qualification_error(
    "native-grandparent",
    missing_finite_search,
    native["interior_checks"],
) is None:
    raise RuntimeError("finite proof-relevant search was absent from a native claim")

interior_recheck = dict(native["interior_checks"])
interior_recheck[
    "prime-regular-kernel-checking-interior-check"
] = 1
if BENCH.native_calculus_qualification_error(
    "native-grandparent", native["native_calculus"], interior_recheck
) is None:
    raise RuntimeError("an interior recheck retained a native hot-path claim")

injected = BENCH.parse_checking_routes(route_lines(1))
if injected["qualification"] == "prime-native-regular":
    raise RuntimeError("injected HE applicability retained the native label")
if injected["legacy_he_applicability_checks"] != 1:
    raise RuntimeError("injected HE applicability was not retained in the receipt")

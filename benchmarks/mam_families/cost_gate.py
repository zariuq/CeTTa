"""Assess paired instruction counts against identical-artifact run variation.

Callers establish artifact identity and a common workload, CPU, environment and
runtime dependencies. Each series maps a paired trial identifier to a checked
measurement. The observed control range is a resolution, not a confidence bound.
"""
from fractions import Fraction
from statistics import median


def instruction_gate(baseline, candidate, control_a, control_b):
    series = (baseline, candidate, control_a, control_b)
    keys = set(baseline)
    if len(keys) < 5 or any(set(samples) != keys for samples in series):
        return dict(status="inconclusive", accepted=False,
                    reason="Five complete matched pairs are required in every series.")
    for samples in series:
        for row in samples.values():
            count = row.get("instructions")
            if (row.get("correct") is not True or row.get("rc") != 0
                    or row.get("timed_out") is not False
                    or type(count) is not int or count <= 0):
                return dict(status="unavailable", accepted=False,
                            reason="An answer check or instruction measurement is unavailable.")
            observation = row.get("observation_sha256", row.get("stdout_sha256"))
            if not isinstance(observation, str) or len(observation) != 64:
                return dict(status="unavailable", accepted=False,
                            reason="A checked observation identity is missing.")
    observations = {row.get("observation_sha256", row.get("stdout_sha256"))
                    for samples in series for row in samples.values()}
    if len(observations) != 1:
        return dict(status="unavailable", accepted=False,
                    reason="The checked observations differ.")

    noise = max(Fraction(abs(control_b[k]["instructions"] - control_a[k]["instructions"]),
                         min(control_a[k]["instructions"], control_b[k]["instructions"]))
                for k in keys)
    differences = [Fraction(candidate[k]["instructions"] - baseline[k]["instructions"],
                            baseline[k]["instructions"]) for k in sorted(keys)]
    lower, upper = min(differences), max(differences)
    if lower > noise:
        status = "regression"
    elif upper < -noise:
        status = "improvement"
    elif upper <= noise:
        status = "no_detected_regression"
    else:
        status = "inconclusive"
    return dict(status=status, accepted=status in ("improvement", "no_detected_regression"),
                trials=len(keys), calibration_percent=float(100 * noise),
                paired_percent=[float(100 * value) for value in differences],
                paired_median_percent=float(100 * median(differences)),
                paired_range_percent=[float(100 * lower), float(100 * upper)])


def layout_gate(baselines, candidates, control_a, control_b):
    """The follow-up for a flagged comparison: each side also linked from the
    same objects in other orders.  A regression is confirmed when every
    candidate build exceeds every baseline build by more than the
    identical-artifact spread; an improvement likewise.  Anything else is a
    layout-sized difference, reported with its size."""
    def build_median(samples):
        return median(row["instructions"] for row in samples.values())
    base = [build_median(samples) for samples in baselines]
    cand = [build_median(samples) for samples in candidates]
    spread = max(abs(control_a[k]["instructions"] - control_b[k]["instructions"])
                 for k in control_a)
    if min(cand) - max(base) > spread:
        status = "confirmed_regression"
    elif min(base) - max(cand) > spread:
        status = "confirmed_improvement"
    else:
        status = "layout_sized"
    middle = median(base)
    return dict(status=status, baseline_builds=base, candidate_builds=cand,
                control_spread=spread,
                median_percent=float(100 * Fraction(median(cand) - middle, middle)))

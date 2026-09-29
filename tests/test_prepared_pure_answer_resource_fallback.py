"""A producer publishes every occurrence of a finite answer stream, however
large, against the canonical route; it honors semantic fuel, and later
invocations reuse it.  Published answers are the call's result, not
speculative scratch, so the speculation purse does not bound them."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def runtime_counters(stderr, stats_enabled):
    counters = {}
    for line in stderr.splitlines():
        fields = line.split()
        assert (stats_enabled and len(fields) == 3 and
                fields[0] == "runtime-counter"), line
        counters[fields[1]] = int(fields[2])
    return counters


def run(binary, source, language, stats_enabled, environment,
        semantic_fuel=None):
    command = [str(binary.resolve()), "--lang", language]
    if language == "he":
        command += ["--profile", "extended"]
    if semantic_fuel is not None:
        command += ["--fuel", semantic_fuel]
    if stats_enabled:
        command += ["--emit-runtime-stats"]
    command.append(str(source))
    return subprocess.run(command, capture_output=True, text=True,
                          env=environment, check=True)


def check_large_publication(binary, stats_enabled):
    payload = "a" * 1_200_000
    runtime = Path.cwd() / "runtime"
    runtime.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="prepared-pure-publication-", dir=runtime) as directory:
        source = Path(directory) / "probe.metta"
        source.write_text(
            "(= (prepared-publication-copy $x) $x)\n"
            "(= (prepared-publication-copy $x) $x)\n"
            f'!(prepared-publication-copy "{payload}")\n')
        environment = dict(
            os.environ, CETTA_GC="1", CETTA_GC_BUDGET_MB="1")
        candidate = run(
            binary, source, "he", stats_enabled, environment)
        reference = run(
            binary, source, "he", stats_enabled, environment, "1000000")
        expected = f'["{payload}", "{payload}"]\n'
        assert candidate.stdout == expected, "publication changed occurrences"
        assert candidate.stdout == reference.stdout, "publication differs from canonical"
        if stats_enabled:
            prefix = "prepared-pure-answer-producer-"
            candidate_counters = runtime_counters(candidate.stderr, True)
            reference_counters = runtime_counters(reference.stderr, True)
            expected_candidate = {
                "admission": 1,
                "commit": 1,
                "decline": 0,
                "resource-decline": 0,
                "answer": 2,
            }
            for name, expected_count in expected_candidate.items():
                assert candidate_counters[prefix + name] == expected_count, (
                    "publication", name,
                    candidate_counters[prefix + name], expected_count)
                assert reference_counters[prefix + name] == 0, (
                    "canonical publication", name,
                    reference_counters[prefix + name])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).with_name("prepared_pure_answer_resource_fallback.metta")
    # The speculation purse is the collection budget; the middle stream's
    # published occurrences exceed one mebibyte and are all published.
    environment = dict(os.environ, CETTA_GC="1", CETTA_GC_BUDGET_MB="1")
    for language in ("he", "petta", "prime"):
        streams = [["(leaf)"] * count for count in (2, 32768, 2)]
        expected = "".join(
            ("\n".join(stream) if language == "petta"
             else "[" + ", ".join(stream) + "]") + "\n"
            for stream in streams
        )
        for semantic_fuel in (None, "1000000"):
            result = run(args.binary, source, language, args.stats,
                         environment, semantic_fuel)
            assert result.stdout == expected, (language, semantic_fuel,
                                               "answer occurrences differ")
            counters = runtime_counters(result.stderr, args.stats)
            if args.stats:
                prefix = "prepared-pure-answer-producer-"
                admitted = language != "prime" and semantic_fuel is None
                expected_counters = {
                    "resource-decline": 0,
                    "admission": 3 if admitted else 0,
                    "commit": 3 if admitted else 0,
                    "decline": 0,
                    "answer": 32772 if admitted else 0,
                }
                for name, expected_count in expected_counters.items():
                    assert counters[prefix + name] == expected_count, (
                        language, semantic_fuel, name, counters[prefix + name],
                        expected_count)
    check_large_publication(args.binary, args.stats)
    print("PASS: producers publish every occurrence beyond the speculation purse, "
          "honor semantic fuel, and permit later invocation reentry")


if __name__ == "__main__":
    main()

"""Compare composed numeric fragments with semantic-fuel canonical execution."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(binary, source, language, reference, prefer_rationals=False):
    command = [str(binary), "--lang", language, "--emit-runtime-stats"]
    if language == "he":
        command += ["--profile", "extended"]
    if prefer_rationals:
        command.append("--prefer-rationals")
    if reference:
        # Logical step accounting excludes speculative compiled execution.
        command += ["--fuel", "1000000"]
    command.append(str(source))
    process = subprocess.run(command, capture_output=True, text=True, timeout=60)
    # An uncaught PeTTa error is a diagnostic and exits with status 2.
    # Preserve its payload in the comparison, separately from answer lines.
    error_prefix = "error: uncaught PeTTa error: "
    errors = [line.removeprefix(error_prefix)
              for line in process.stderr.splitlines()
              if line.startswith(error_prefix)]
    stopped = language == "petta" and process.returncode == 2 and len(errors) == 1
    if process.returncode and not stopped:
        raise AssertionError((source.read_text(), language, reference,
                              process.returncode, process.stdout, process.stderr))
    counters = {}
    for line in process.stderr.splitlines():
        if stopped and line.startswith(error_prefix):
            continue
        fields = line.split()
        if len(fields) != 3 or fields[0] != "runtime-counter":
            raise AssertionError(f"unexpected diagnostic: {line}")
        counters[fields[1]] = int(fields[2])
    return process.stdout, counters, process.returncode, errors


def programs():
    pairs = [("1.5", "2.0"), ("-0.0", "0.0"), ("2", "0.5"),
             ("0.5", "-2"), ("9007199254740993", "9007199254740992.0"),
             ("9223372036854775807", "1"), ("-7", "3")]
    operations = ["+", "-", "*", "<", ">", "<=", ">="]
    for variant in range(3):
        prefix = ["measure", "renamed", "long-common-prefix-function"][variant]
        forms = []
        if variant == 2:
            forms += [f"(= (unrelated-{i} $x) (Data $x))" for i in range(101)]
        for index, operation in enumerate(operations):
            name = f"{prefix}-{index}"
            body = f"({operation} $left $right)"
            if variant:
                forms.append(f"(= ({name}-helper $a $b) ({operation} $a $b))")
                body = f"({name}-helper $left $right)"
            forms.append(f"(= ({name} $left $right) {body})")
            forms += [f"!({name} {left} {right})" for left, right in pairs]
        yield prefix, "\n".join(forms), len(pairs) * len(operations), True

    # numeric-eq is CeTTa's own operator: HE and Prime compile it, and PeTTa,
    # whose reference does not define it, keeps it data.
    forms = ["(= (numeric-eq-measure $left $right) (numeric-eq $left $right))"]
    forms += [f"!(numeric-eq-measure {left} {right})" for left, right in pairs]
    yield "numeric-eq", "\n".join(forms), len(pairs), ("he", "prime")

    yield "recursive", """(= (iterate-number $n $x)
   (if (== $n 0) $x
       (iterate-number (- $n 1) (+ (* $x 0.5) 1.0))))
!(iterate-number 200 0.0)
""", 1, True
    yield "exceptional-floats", """(= (float-result $x $y) (Results (+ $x $y) (* $x $y) (< $x $y)))
!(float-result (/ 1.0 0.0) 0.0)
!(float-result (/ 0.0 0.0) 1.0)
""", {"petta": 0, "he": 2, "prime": 2}, ("he", "prime")
    # Neither failure nor unsupported values may become a successful number.
    yield "unsupported", """(= (number-result $x $y) (+ $x $y))
!(number-result datum 1)
""", {"petta": 0, "he": 1, "prime": 1}, False
    yield "rational", """(= (ratio $x $y) (+ (/ $x $y) (/ 1 3)))
!(ratio 1 2)
(= (ratio-less $x $y) (< (/ $x 3) (/ $y 2)))
!(ratio-less 2 3)
""", 2, True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    # Debug diagnostics are not part of this gate's counter protocol.
    os.environ.pop("CETTA_PREPARED_PURE_DEBUG", None)
    runtime = Path("runtime")
    runtime.mkdir(exist_ok=True)
    checks = 0
    with tempfile.TemporaryDirectory(prefix="prepared-numeric-", dir=runtime) as tmp:
        source = Path(tmp) / "program.metta"
        for name, program, outputs, compiled in programs():
            source.write_text(program)
            for language in ("petta", "he", "prime"):
                candidate, counters, status, errors = run(
                    binary, source, language, False, name == "rational")
                reference, reference_counters, reference_status, reference_errors = run(
                    binary, source, language, True, name == "rational")
                label = f"{language}/{name}"
                assert (candidate, status, errors) == (reference, reference_status, reference_errors), \
                    (label, candidate, status, errors, reference, reference_status, reference_errors)
                if language == "petta" and name in ("exceptional-floats", "unsupported"):
                    formal = "(evaluation_error zero_divisor)" if name == "exceptional-floats" \
                        else "(type_error evaluable (/ datum 0))"
                    assert status == 2 and errors[0].startswith(f"(Error {formal} "), (label, errors)
                # SWI-PeTTa raises for a zero divisor and the file stops
                # there; HE and Prime keep IEEE results.
                expected_lines = outputs[language] \
                    if isinstance(outputs, dict) else outputs
                assert len(candidate.splitlines()) == expected_lines, \
                    (label, candidate)
                assert reference_counters["prepared-pure-call-commit"] == 0, label
                if compiled is True or (compiled and language in compiled):
                    assert "Error" not in candidate, (label, candidate)
                    assert counters["prepared-pure-call-commit"] > 0, label
                    assert counters["prepared-pure-call-decline"] == 0, (label, counters)
                else:
                    assert counters["prepared-pure-call-decline"] > 0, label
                checks += 1
    print(f"PASS: prepared numeric realizations ({checks} semantic/coverage cases)")


if __name__ == "__main__":
    main()

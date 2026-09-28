"""Conservative process-trace validation for curriculum evidence."""

INVALID = "run-invalid: "


def failed_output(line: str) -> bool:
    return (line in {"[False]", "[Refuted]", "[Undetermined]", "[Incomplete]"}
            or line.startswith(("[(Error", "[(Refuted", "[(Undetermined", "[(Incomplete", "error:", INVALID)))


def trace_failure(code: int, lines: list[str], stderr: str, targets: int = 1) -> str:
    """The final target outputs may be negative; their setup must have succeeded."""
    if code != 0:
        return INVALID + f"process exited {code}"
    if stderr.strip():
        return INVALID + "stderr: " + stderr.strip().splitlines()[0][:240]
    if len(lines) < targets:
        return INVALID + f"expected {targets} target outputs, received {len(lines)}"
    setup = lines[:-targets] if targets else lines
    for index, line in enumerate(setup):
        if failed_output(line):
            return INVALID + f"setup output {index + 1}: " + line[:240]
    return ""


def final_output(code: int, lines: list[str], stderr: str) -> str:
    failure = trace_failure(code, lines, stderr)
    return failure if failure else lines[-1]

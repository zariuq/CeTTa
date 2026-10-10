#!/usr/bin/env python3
"""In-process retrieval growth and tensor timings over generated resident values."""
import argparse
import json
from pathlib import Path
import re
import statistics
import sys
import tempfile

from compact import ROOT, Runtime, current_image_peak_rss_kib, digest, require, write_report


def expression(*items):
    return "(" + " ".join(items) + ")"


def case(value, variable, body):
    return expression("case", value, expression(expression(variable, body)))


def timed(label, body, expected):
    result = expression("Capacity", label, expression("-", "$end", "$start"),
                        expression("==", "$value", expected))
    return case("(system:monotonic-ns)", "$start",
                case(body, "$value", case("(system:monotonic-ns)", "$end", result)))


REBUILD_CONTROL = """
(= (capacity:rebuild-validate ()) ())
(= (capacity:rebuild-validate (cons (RetrievalFailure $id $reason) $rest))
   (RetrievalFailure $id $reason))
(= (capacity:rebuild-validate (cons ($score $hit) $rest))
   (case (capacity:rebuild-validate $rest)
     (((RetrievalFailure $id $reason) (RetrievalFailure $id $reason))
      ($valid (cons-atom ($score $hit) $valid)))))
"""


def retrieval_source(count, dimension, repeats, control):
    pattern = "(Embedding $id $revision profile $vector group)"
    setup = []
    for index in range(count):
        value = index % 17 - 8
        setup.append(expression("let", "$vector",
                                f"(tensor:full f64 ({dimension}) {value})",
                                f"(add-atom &capacity (Embedding {index} 1 profile $vector group))"))
    best = [index for index in range(count) if index % 17 == 16][:5]
    expected = expression(*(f"({float(8 * dimension)} (RetrievalHit {i} 1 profile group))"
                            for i in best))
    operations = [("validate", "(retrieval:validate $scored)", "$scored"),
                  ("score-selection", "(retrieval:exact-dot $query $records 5)", expected),
                  ("space-search", "(retrieval:search-dot &capacity profile $query 5)", expected)]
    if control:
        operations.insert(1, ("rebuild-control", "(capacity:rebuild-validate $scored)", "$scored"))
    samples = [timed(label, body, expected)
               for label, body, expected in operations for _ in range(repeats + 1)]
    checks = expression("collapse", expression("superpose", expression(*samples)))
    complete_scores = expression("if", f"(== (size-atom $scored) {count})", checks,
                                 "(CapacityFailure missing-scores)")
    complete_records = expression("if", f"(== (size-atom $records) {count})",
        expression("let", "$scored", "(collapse (retrieval:score-dot $query (superpose $records)))",
                   complete_scores), "(CapacityFailure missing-records)")
    body = expression("let", "$query", f"(tensor:full f64 ({dimension}) 1)",
             expression("let", "$records", f"(collapse (match &capacity {pattern} {pattern}))",
                        complete_records))
    return "!(import! &self (library lib_retrieval))\n!(import! &self (library system))\n" + (
        REBUILD_CONTROL if control else "") + "!(bind! &capacity (new-space))\n" + (
        "\n".join("!" + value for value in setup)) + "\n!" + body + "\n"


def measure(runtime, source, repeats):
    output, process = runtime.run(source)
    values = re.findall(r"\(Capacity ([a-z-]+) (\d+) (true|false)\)", "\n".join(output))
    require(values and all(valid == "true" for _, _, valid in values),
            "Missing or incorrect capacity result: " + "\n".join(output)[-1000:])
    grouped = {}
    for label, duration, _ in values:
        grouped.setdefault(label, []).append(int(duration))
    rows = {}
    for label, samples in grouped.items():
        require(len(samples) == repeats + 1, "Missing repeat for " + label)
        measured = [value / 1e9 for value in samples[1:]]
        require(all(value > 0 for value in measured), "Invalid monotonic timing")
        rows[label] = {"median_seconds": statistics.median(measured),
                       "samples_seconds": measured, "warmup_seconds": samples[0] / 1e9}
    return rows, process


def growth(rows, label, overhead_factor):
    small, large = rows[0], rows[-1]
    size_ratio = large["records"] / small["records"]
    ratio = large["stages"][label]["median_seconds"] / small["stages"][label]["median_seconds"]
    return {"record_ratio": size_ratio, "time_ratio": ratio,
            "maximum_time_ratio": size_ratio * overhead_factor,
            "within_declared_growth": ratio <= size_ratio * overhead_factor}


def run(options):
    require(options.dimension > 0 and options.repeats > 0 and options.threads > 0 and
            options.bulk_length > 0 and options.growth_overhead >= 1 and
            len(options.records) >= 2 and min(options.records) >= 85 and
            options.records == sorted(set(options.records)), "Invalid capacity workload")
    binary = options.binary.resolve()
    require(binary.is_file(), "A tensor-enabled binary is required")
    report = {"schema": "cetta-tensor-capacity-v1", "status": "running",
              "programme_complete": False, "binary_sha256": digest(binary),
              "benchmark_sha256": digest(Path(__file__)),
              "runtime_driver_sha256": digest(Path(__file__).with_name("compact.py")),
              "retrieval_source_sha256": digest(ROOT / "lib/petta/lib_retrieval.metta"),
              "dimension": options.dimension, "threads": options.threads,
              "timing_scope": "in-process after construction and warmup; result checks follow the clock",
              "growth_gate_requested": options.check_growth, "rows": []}
    with tempfile.TemporaryDirectory(prefix="cetta-tensor-capacity-", dir=options.work_dir) as folder:
        runtime = Runtime(binary, Path(folder), options.threads)
        for count in options.records:
            source = retrieval_source(count, options.dimension, options.repeats, options.rebuild_control)
            stages, process = measure(runtime, source, options.repeats)
            required = {"validate", "score-selection", "space-search"}
            if options.rebuild_control:
                required.add("rebuild-control")
            require(set(stages) == required, "Missing or unexpected retrieval stage")
            report["rows"].append({"records": count, "payload_bytes": count * options.dimension * 8,
                                   "stages": stages, "whole_process": process})
        report["validation_growth"] = growth(report["rows"], "validate", options.growth_overhead)
        if options.rebuild_control:
            report["rebuild_control_growth"] = growth(report["rows"], "rebuild-control", options.growth_overhead)
            report["growth_control"] = ("rejected" if not report["rebuild_control_growth"]["within_declared_growth"]
                                        else "not-distinguished")
        samples = [timed("dot", "(tensor:dot $vector $vector)", str(float(options.bulk_length)))
                   for _ in range(options.repeats + 1)]
        source = "!(import! &self (library lib_tensor))\n!(import! &self (library system))\n!" + expression(
            "let", "$vector", f"(tensor:full f64 ({options.bulk_length}) 1)",
            expression("collapse", expression("superpose", expression(*samples))))
        measured, process = measure(runtime, source, options.repeats)
        require(set(measured) == {"dot"}, "Missing dot measurement")
        report["resident_dot"] = {"elements": options.bulk_length, **measured["dot"],
                                  "whole_process": process}
        report["resources"] = {"largest_transient_input_bytes": runtime.largest_input,
                               "retained_dataset_bytes": 0,
                               "native_processes": runtime.processes,
                               "parent_max_rss_kib": current_image_peak_rss_kib()}
    report["status"] = "pass"
    if options.check_growth and not report["validation_growth"]["within_declared_growth"]:
        report["status"] = "fail"
        report["failure"] = "Validation exceeds the declared growth ratio"
    elif options.check_growth and options.rebuild_control and report["growth_control"] != "rejected":
        report["status"] = "fail"
        report["failure"] = "Growth check did not distinguish the rebuilding control"
    write_report(report, options.output)
    require(report["status"] == "pass", report.get("failure", "Capacity check failed"))
    print(f"PASS: in-process retrieval at {options.records}; "
          f"validation growth {report['validation_growth']['time_ratio']:.2f}x")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "cetta")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/tensor-capacity.json")
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--records", type=int, nargs="+", default=[2000, 8000])
    parser.add_argument("--dimension", type=int, default=64)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--bulk-length", type=int, default=1000000)
    parser.add_argument("--growth-overhead", type=float, default=1.5)
    parser.add_argument("--check-growth", action="store_true")
    parser.add_argument("--rebuild-control", action="store_true")
    options = parser.parse_args()
    write_report({"schema": "cetta-tensor-capacity-v1", "status": "running"}, options.output)
    try:
        run(options)
    except Exception as error:
        # Preserve a measured failed gate; overwrite stale receipts for other failures.
        saved = json.loads(options.output.read_text())
        if saved.get("status") != "fail":
            write_report({"schema": "cetta-tensor-capacity-v1", "status": "fail",
                          "failure": str(error), "programme_complete": False}, options.output)
        raise


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, ImportError) as error:
        print("FAIL:", error, file=sys.stderr)
        sys.exit(1)

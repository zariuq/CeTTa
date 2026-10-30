#!/usr/bin/env python3
"""Build selected configurations with fresh compiler-specific artifacts."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    "core": {"BUILD": "core"},
    "minimal": {"BUILD": "core", "ENABLE_GMP": "0", "ENABLE_LIB_PROLOG": "0"},
    "python": {"BUILD": "python"},
    "mork": {"BUILD": "mork"},
    "main": {"BUILD": "main"},
    "pathmap": {"BUILD": "pathmap"},
    "full": {"BUILD": "full"},
    "stats": {"BUILD": "core", "ENABLE_RUNTIME_STATS": "1", "ENABLE_LIB_PROLOG": "0"},
    "pic": {"BUILD": "core", "ENABLE_PIC": "1", "ENABLE_LIB_PROLOG": "0"},
    "json-off": {"BUILD": "core", "ENABLE_JSON_GSLT": "0", "ENABLE_LIB_PROLOG": "0"},
    "prime-classic": {"BUILD": "core", "ENABLE_PRIME_NEED_HEAP_INDEX": "0",
                      "ENABLE_PRIME_EVAL_STACK": "0", "ENABLE_LIB_PROLOG": "0"},
    "provenance": {"BUILD": "core", "CETTA_PROVENANCE_ASSERT": "1", "ENABLE_LIB_PROLOG": "0"},
    "http": {"BUILD": "core", "ENABLE_HTTP": "1", "ENABLE_LIB_PROLOG": "0"},
    "durable": {"BUILD": "core", "ENABLE_DURABLE": "1", "ENABLE_LIB_PROLOG": "0"},
}
DEFAULT_PROFILES = ["core", "minimal", "python", "stats"]
SMOKE = """(= (matrix-step $x) (+ $x 1))
(matrix-row 1)
(matrix-row 1)
(matrix-row 2)
!(matrix-step 2)
!(match &self (matrix-row $value) $value)
"""


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def smoke_observation(language, output):
    lines = output.splitlines()
    if language == "petta":
        return lines[:1] == ["3"] and Counter(lines[1:]) == Counter(["1", "1", "2"])
    if len(lines) != 2 or lines[0] != "[3]":
        return False
    return (lines[1].startswith("[") and lines[1].endswith("]") and
            Counter(lines[1][1:-1].replace(",", " ").split()) == Counter(["1", "1", "2"]))


def run_case(compiler, profile, arguments, directory, run_id):
    settings = dict(PROFILES[profile])
    compiler_words = shlex.split(compiler)
    executable = shutil.which(compiler_words[0])
    if not executable:
        return {"compiler": compiler, "profile": profile, "passed": False,
                "error": "compiler is unavailable"}
    compiler_words[0] = executable
    version = subprocess.run(compiler_words + ["--version"], capture_output=True,
                             text=True, timeout=30, check=True).stdout.splitlines()[0]
    identity = hashlib.sha256(json.dumps([compiler_words, version, settings]).encode()).hexdigest()[:12]
    tag = settings["BUILD"] + ".matrix-" + run_id + "-" + identity
    case = directory / (profile + "-" + identity)
    case.mkdir()
    invocations = case / "compiler.jsonl"
    wrapper = case / "cc"
    # A single append records each real invocation before exec, including
    # compiles issued by bootstrap recipes whose shell commands are quiet.
    wrapper.write_text("#!" + sys.executable + "\n" +
        "import json,os,sys\n" +
        "fd=os.open(" + repr(str(invocations)) + ",os.O_CREAT|os.O_WRONLY|os.O_APPEND,0o600)\n" +
        "record={'cwd':os.getcwd(),'arguments':sys.argv[1:]}\n" +
        "os.write(fd,(json.dumps(record)+'\\n').encode());os.close(fd)\n" +
        "command=" + repr(compiler_words) + "\n" +
        "os.execvp(command[0],command+sys.argv[1:])\n")
    wrapper.chmod(0o700)
    environment = {key: value for key, value in os.environ.items()
                   if key not in {"MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"} and
                   not key.startswith("CETTA_")}
    environment["CARGO_BUILD_JOBS"] = str(arguments.jobs)
    command = ["make", "--no-print-directory", "-j" + str(arguments.jobs),
               "CC=" + shlex.quote(str(wrapper)), "CETTA_TEST_ISOLATED=1", "BUILD_OBJ_TAG=" + tag]
    command += [key + "=" + value for key, value in settings.items()]
    command += ["OP_LANG_PREPARE_TOOL_V1=" + str(case / "emit-operational-reader"),
                "all", "test-list-value-v1", "test-process-stream-v1",
                "test-python-build-config", "test-lib-prolog-build-config"]
    started = time.monotonic()
    result = {"compiler": compiler, "compiler_version": version, "profile": profile,
              "settings": settings, "command": command, "passed": False}
    try:
        with (case / "build.log").open("w") as log:
            built = subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, timeout=arguments.timeout)
        result["build_returncode"] = built.returncode
        if built.returncode:
            result["error"] = "build or native/link configuration check failed"
            return result
        records = [json.loads(line) for line in invocations.read_text().splitlines()]
        compiled = [record for record in records if "-c" in record["arguments"] and
                    any(word.endswith(".c") for word in record["arguments"])]
        # SQLite and the optional bridge's dependencies have separate warning
        # policies. Record the working directory so a dependency's src/arena.c
        # cannot be mistaken for CeTTa's translation unit with the same name.
        owned = [record["arguments"] for record in compiled if
                 Path(record["cwd"]) == ROOT and
                 "vendor/sqlite/sqlite3.c" not in record["arguments"]]
        if not owned or any("-Werror" not in call for call in owned):
            result["error"] = "fresh warning-strict source compilation was not established"
            return result
        critical_sources = {"src/atom.c", "src/eval.c", "tools/langdef_compile_v1.c"}
        observed_sources = {word for call in owned for word in call if word.endswith(".c")}
        if not critical_sources <= observed_sources:
            result["error"] = "critical translation units were not freshly compiled"
            return result
        result["compiled_translation_units"] = len(compiled)
        result["warning_strict_translation_units"] = len(owned)
        result["vendor_translation_units"] = len(compiled) - len(owned)
        stats = settings.get("ENABLE_RUNTIME_STATS") == "1"
        binary = ROOT / "runtime" / ("cetta-" + tag + ("-runtime-stats" if stats else ""))
        result["binary_sha256"] = file_hash(binary)
        object_file = ROOT / "src" / ("atom." + tag + (".runtime-stats" if stats else "") + ".o")
        if shutil.which("readelf"):
            comment = subprocess.run(["readelf", "--string-dump=.comment", str(object_file)],
                                     capture_output=True, text=True, timeout=30, check=True)
            result["object_compiler_comment"] = comment.stdout
        version_run = subprocess.run([str(binary), "-v"], cwd=ROOT, env=environment,
                                     capture_output=True, text=True, timeout=30)
        result["binary_version"] = version_run.stdout + version_run.stderr
        if version_run.returncode or "(" + settings["BUILD"] + ")" not in result["binary_version"]:
            result["error"] = "linked binary does not report the requested build mode"
            return result
        source = case / "smoke.metta"
        source.write_text(SMOKE)
        observations = []
        for language in ["petta", "he", "prime"]:
            observed = subprocess.run([str(binary), "--lang", language, str(source)],
                                      cwd=ROOT, env=environment, capture_output=True,
                                      text=True, timeout=60)
            observations.append({"language": language, "returncode": observed.returncode,
                                 "stdout": observed.stdout, "stderr": observed.stderr})
            if observed.returncode or observed.stderr or not smoke_observation(language, observed.stdout):
                result["error"] = "language smoke observation failed"
        result["observations"] = observations
        result["passed"] = "error" not in result
    except (subprocess.SubprocessError, OSError) as error:
        result["error"] = str(error)
    finally:
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (case / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", action="append", help="compiler command; repeat to compare compilers")
    parser.add_argument("--profile", action="append", choices=PROFILES)
    parser.add_argument("--all-profiles", action="store_true")
    parser.add_argument("--list-profiles", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--output-dir", type=Path)
    arguments = parser.parse_args()
    if arguments.list_profiles:
        print("\n".join(PROFILES))
        return 0
    if arguments.jobs < 1 or arguments.timeout < 1:
        parser.error("jobs and timeout must be positive")
    run_id = uuid.uuid4().hex[:8]
    directory = (arguments.output_dir or ROOT / "runtime" / "build-matrix" / run_id).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    profiles = list(PROFILES) if arguments.all_profiles else arguments.profile or DEFAULT_PROFILES
    results = []
    for compiler in arguments.compiler or [os.environ.get("CC", "gcc")]:
        for profile in profiles:
            result = run_case(compiler, profile, arguments, directory, run_id)
            results.append(result)
            (directory / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            print(("PASS" if result["passed"] else "FAIL") + ": " + compiler + " / " + profile,
                  flush=True)
    passed = sum(row["passed"] for row in results)
    print("Build matrix: " + str(passed) + "/" + str(len(results)) + " passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())

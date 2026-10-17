#!/usr/bin/env python3
"""Cross-profile effect, process ownership, and native-mutation qualification."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/effects-v1-tests")
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    parser.add_argument("--preparation-stats", action="store_true")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / "fixture"
    control, mutant = output / "control", output / "mutant"
    for path, default in ((fixture, None), (control, "control"), (mutant, "mutant")):
        command = ["cc", "-std=c11", "-O2", "-Wall", "-Werror"]
        if default:
            command += [f'-DFIXTURE_DEFAULT="{default}"']
        subprocess.run(command + [str(ROOT / "tests/support/proc_fixture_v1.c"), "-o", str(path)], check=True)
    count = 0
    def run(language, name, source=None, *, flags=(), script=None, argv=()):
        nonlocal count
        if script is None:
            script = output / f"{language}-{name}.metta"
            script.write_text(source, encoding="utf-8")
        command = [str(binary), "--lang", language]
        if language != "prime":
            command += ["--profile", "extended"]
        result = subprocess.run(command + list(flags) + [str(script)] + list(map(str, argv)),
                                cwd=ROOT, capture_output=True, timeout=30)
        (output / f"{language}-{name}.stdout").write_bytes(result.stdout)
        (output / f"{language}-{name}.stderr").write_bytes(result.stderr)
        count += 1
        return result

    def require(result, text, status=0):
        assert result.returncode == status and text in result.stdout, (result.returncode, result.stdout, result.stderr, text)

    examples = ROOT / "tests/effects_v1"
    for language in args.language or ("he", "petta", "prime"):
        if language == "petta":
            require(run(language, "raised-control", script=examples / "outcomes_raised_petta_negative.metta"), b"", status=2)
            raised = run(language, "caught-control", script=examples / "outcomes_caught_petta_positive.metta")
            require(raised, b"Error")
            assert b"DivisionByZero" in raised.stdout or b"zero_divisor" in raised.stdout, raised
            assert b"42\n" in raised.stdout, raised
        for name, text in (
            ("outcomes_occurrences_positive", b"(taken (a a) complete)"),
            ("outcomes_empty_positive", b"(taken () complete)"),
            ("lifetime_resume_positive", b"(taken (b) more-possible)"),
            ("lifetime_status_positive", b"proc:consumed"),
            ("lifetime_closed_negative", b"proc:invalid-handle"),
            ("sequence_inert_positive", b"(Error ordinary payload)"),
            ("process_stdin_positive", b"(proc:input 5 5)"),
            ("process_exit_positive", b"(proc:exited 1)"),
            ("process_missing_negative", b"proc:launch-failed"),
        ):
            result = run(language, name, script=examples / f"{name}.metta")
            require(result, text)
            assert b"forbidden" not in result.stdout, result.stdout
        result = run(language, "outcomes_pending_negative", script=examples / "outcomes_pending_negative.metta")
        require(result, b"WorkBudgetRequiresResumableRoute" if language == "prime" else b"(taken () paused)")
        if language != "prime":
            result = run(language, "pause-resume", script=examples / "lifetime_pause_resume_positive.metta")
            require(result, b"(taken () paused) (taken (1 2) complete)")
        result = run(language, "isolated-owners", script=examples / "lifetime_isolation_positive.metta")
        require(result, b"first-owner")
        assert b"second-owner" in result.stdout and b"proc:consumed" not in result.stdout, result
        result = run(language, "sequence_order_positive", script=examples / "sequence_order_positive.metta", flags=("--no-results",))
        assert result.returncode == 0 and result.stdout == b"first\nsecond\n", result
        result = run(language, "sequence_replay_negative", script=examples / "sequence_replay_negative.metta")
        assert result.returncode == 0 and result.stdout.count(b"once\n") == 1, result
        preparation_flags = ("--emit-runtime-stats",) if args.preparation_stats else ()
        result = run(language, "preparation-reuse", script=examples / "preparation_reuse_positive.metta", flags=preparation_flags)
        require(result, b"done")
        if args.preparation_stats and language == "he":
            assert b"runtime-counter prepared-map-safety-check 1\n" in result.stderr, result.stderr
            assert b"runtime-counter prepared-map-safety-reuse 199\n" in result.stderr, result.stderr
        result = run(language, "preparation-revision", script=examples / "preparation_revision_negative.metta", flags=preparation_flags)
        require(result, b"PreparationChanged")
        assert result.stdout.count(b"revision-effect\n") == 1, result
        if args.preparation_stats and language == "he":
            assert b"runtime-counter prepared-map-safety-check 2\n" in result.stderr, result.stderr
        if language == "he":
            require(run(language, "raw-callback-data", script=examples / "preparation_raw_callback_positive.metta"), b'("a" "b")')
            raw_effect = run(language, "raw-callback-effects", script=examples / "preparation_raw_callback_negative.metta", flags=("--no-results",))
            assert raw_effect.returncode == 0 and raw_effect.stdout == b"raw-callback-effect\n" * 2, raw_effect
        result = run(language, "completion_bytes_positive", script=examples / "completion_bytes_positive.metta", flags=("--no-results",))
        assert result.returncode == 0 and result.stdout == b"before\0after\xff\n" and result.stderr == b"diagnostic\n", result
        result = run(language, "completion_exit_positive", script=examples / "completion_exit_positive.metta", flags=("--no-results",))
        assert result.returncode == 7 and result.stdout == b"published-before-exit\n", result
        prior = run(language, "exit-prior-answer", "!(import! &self system)\n!(+ 1 2)\n!(system:exit 7)\n!(println! forbidden-after-exit)\n")
        assert prior.returncode == 7 and b"3" in prior.stdout and b"forbidden-after-exit" not in prior.stdout, prior
        if language == "petta":
            aborted_test = run(language, "failed-test-display", script=ROOT / "tests/petta/test_failure_exit.metta")
            assert aborted_test.returncode == 1 and b"true\n" not in aborted_test.stdout and b"should-not-run" not in aborted_test.stdout, aborted_test
        # Failed output is a failed acknowledgement, even with answer display
        # suppressed; the write must not kill the host before owner cleanup.
        closed_read, closed_write = os.pipe()
        os.close(closed_read)
        command = [str(binary), "--lang", language]
        if language != "prime":
            command += ["--profile", "extended"]
        closed_descriptor = subprocess.run(command + ["--no-results",
            str(examples / "completion_closed_descriptor_negative.metta")],
            cwd=ROOT, preexec_fn=lambda: os.close(1), stderr=subprocess.PIPE, timeout=30)
        count += 1
        assert closed_descriptor.returncode == 1, closed_descriptor
        try:
            result = subprocess.run(command + ["--no-results", str(examples / "sequence_order_positive.metta")],
                                    cwd=ROOT, stdout=closed_write, stderr=subprocess.PIPE, timeout=30)
            count += 1
            assert result.returncode == 1, result
        finally:
            os.close(closed_write)
        if language != "prime":
            read_fd, write_fd = os.pipe()
            os.close(read_fd)
            try:
                failed_output = subprocess.run(command + ["--no-results", "--run-contract",
                    str(examples / "completion_bytes_positive.metta")], cwd=ROOT,
                    stdout=subprocess.PIPE, stderr=write_fd, timeout=30)
                count += 1
                assert failed_output.returncode == 1, failed_output
                unpublished = subprocess.run(command + ["--no-results", "--run-contract", "--run-report",
                    str(output / "absent-parent/receipt.json"), str(examples / "completion_bytes_positive.metta")],
                    cwd=ROOT, stdout=subprocess.PIPE, stderr=write_fd, timeout=30)
                count += 1
                assert unpublished.returncode == 1, unpublished
            finally:
                os.close(write_fd)
        if language != "prime":
            report = output / f"{language}-early-exit.json"
            result = run(language, "completion_contract_negative", script=examples / "completion_contract_negative.metta",
                         flags=("--no-results", "--run-contract", "--run-report", str(report)))
            document = json.loads(report.read_text())
            assert result.returncode == 1 and document["queries_unsettled"] > 0, (result, document)
            assert b"without a final run receipt" not in result.stderr, result.stderr

        imports = "!(import! &self proc)\n!(import! &self system)\n"
        def quote(value):
            return json.dumps(str(value))
        def process(name, mode, options="()", raw=False):
            call = f'(proc:run ({quote(fixture)} {quote(mode)}) {options})'
            if raw:
                program = imports + "(= (emit (proc:result $t $c $out $err $capture)) " \
                    "(let $written (system:write stdout $out) (system:write stderr $err)))\n!" + f"(emit {call})\n"
            else:
                program = imports + "!" + call + "\n"
            return run(language, name, program, flags=("--no-results",) if raw else ())
        lines = output / f"{language}-line-records.txt"
        program = f'!(import! &self fs)\n!(let $written (fs:write {quote(lines)} "one\\n\\n") ' \
            f'(fs:read-lines {quote(lines)}))\n'
        require(run(language, "trailing-blank-record", program), b'("one" "")')
        program = f'!(import! &self fs)\n!(let $written (fs:write {quote(lines)} "") ' \
            f'(fs:read-lines {quote(lines)}))\n'
        require(run(language, "empty-file-records", program), b"()")
        result = process("binary-captures", "bytes", raw=True)
        assert result.returncode == 0 and result.stdout == bytes(range(256)) and result.stderr == bytes(range(256)), result
        input_bytes = "0123456789" * 20000
        result = process("duplex", "duplex", f'((stdin {quote(input_bytes)}) (timeout-ms 5000))', raw=True)
        assert result.returncode == 0 and result.stdout == input_bytes.encode() and result.stderr == b"e" * (4096 * 128), result
        require(process("signal", "signal"), b"proc:signaled")
        require(process("timeout", "sleep", "((timeout-ms 40))"), b"proc:timed-out")
        long_wait = imports + f'!(let $owner (proc:spawn ({quote(fixture)} "sleep") ((timeout-ms 500))) ' \
            '(let $observed (proc:wait $owner 2147483648) ' \
            '(let $closed (proc:close $owner) (LongWait $observed))))\n'
        require(run(language, "long-wait-domain", long_wait), b"proc:timed-out")
        require(process("capture-bound", "bytes", "((max-bytes 7))"), b"(proc:capture 7 256 7 256")
        program = imports + '(= (proc:result $t $c $out $err $capture) (println! forbidden-native-reinterpretation))\n' \
            + '(= (proc:exited $code) (println! forbidden-native-field))\n' \
            + '!(proc:run ("/bin/echo" "data") ())\n'
        result = run(language, "native-inert-fields", program)
        require(result, b"proc:result")
        assert b"forbidden-native-reinterpretation" not in result.stdout, result
        assert b"forbidden-native-field" not in result.stdout, result
        require(run(language, "duplicate-options", imports + '!(proc:run ("/bin/echo") ((stdin "a") (stdin "b")))\n'), b"proc:invalid-argument")
        require(run(language, "nul-argv", imports + '!(import! &self str)\n!(let $arg (str:unescape metta "a\\\\x00b") (proc:run ("/bin/echo" $arg) ()))\n'), b"proc:invalid-argument")
        env = f'((cwd {quote(output)}) (env (("CETTA_PROC_FIXTURE" "isolated") ("PATH" "/bin:/usr/bin"))))'
        result = process("environment", "environment", env, raw=True)
        assert result.returncode == 0 and result.stdout == f"{output}\nisolated\n".encode(), result
        pid_file = output / f"{language}-child.pid"
        program = imports + f'!(let $owner (proc:spawn ({quote(fixture)} "sleep" {quote(pid_file)}) ()) ' \
            '(let $pending (proc:wait $owner 50) (let $cancel (proc:cancel $owner) ' \
            '(let $result (proc:await $owner) (let $closed (proc:close $owner) (Cancelled $pending $result))))))\n'
        result = run(language, "cancel-and-reap", program)
        require(result, b"proc:cancelled")
        assert b"proc:pending" in result.stdout, result
        if pid_file.exists():
            pid = int(pid_file.read_text())
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError(("child leaked", language, pid))
        program = imports + '!(let $owner (proc:spawn ("/bin/echo" "once") ()) ' \
            '(let $first (proc:await $owner) (let $second (proc:poll $owner) ' \
            '(let $closed (proc:close $owner) (Consumed $first $second)))))\n'
        require(run(language, "completion-once", program), b"proc:already-consumed")
        # Last-holder release and context teardown must cancel unconsumed jobs.
        started = time.monotonic()
        result = run(language, "abandon", imports + f'!(proc:spawn ({quote(fixture)} "sleep") ())\n')
        assert result.returncode == 0 and time.monotonic() - started < 5, result

        checker = ROOT / "tests/support/check_native_mutation_rejected.metta"
        result = run(language, "mutation-accepted", flags=("--no-results",), script=checker,
                     argv=(control, mutant, "fixture assertion: occurrence preserved"))
        require(result, b"PASS: native control accepted; mutation rejected by fixture assertion: occurrence preserved\n")
        reference = subprocess.run(["python3", str(ROOT / "tests/support/check_native_mutation_rejected.py"),
                                    str(control), str(mutant), "fixture assertion: occurrence preserved"], capture_output=True, timeout=30)
        assert reference.returncode == result.returncode and reference.stdout == result.stdout, (reference, result)
        for name, values in (("wrong-assertion", (control, mutant, "absent witness")),
                             ("wrong-signal", (control, control, "PASS:")),
                             ("bad-control", (mutant, mutant, "fixture assertion")),
                             ("missing-control", (output / "missing", mutant, "fixture assertion")),
                             ("usage", ())):
            result = run(language, "mutation-" + name, flags=("--no-results",), script=checker, argv=values)
            assert result.returncode != 0 and b"PASS: native control" not in result.stdout, (language, name, result)
    print(f"(EffectsV1Summary {count} 0)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Bounded process-stream controls, with an optional real Lean REPL exchange."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
IMPORTS = '''!(import! &self proc)
!(import! &self str)
!(import! &self system)
!(import! &self json)
(= (stream-test:bytes $owner $channel $count $acc)
   (if (>= (str:byte-length $acc) $count) $acc
     (case (proc:read $owner $channel 100)
       (((proc:chunk $part) (stream-test:bytes $owner $channel $count (str:concat $acc $part)))
        (proc:pending (stream-test:bytes $owner $channel $count $acc))
        ($other (StreamTestFailure $other))))))
(= (stream-test:frame $owner $acc)
   (if (> (str:byte-length $acc) 1048576) StreamTestOversize
     (if (str:ends-with $acc "\\n\\n") (json:parse $acc)
       (case (proc:read $owner stdout 100)
         (((proc:chunk $part) (stream-test:frame $owner (str:concat $acc $part)))
          (proc:pending (stream-test:frame $owner $acc))
          ($other (StreamTestFailure $other)))))))
(= (stream-test:request $owner $request)
   (case (proc:write $owner (str:concat (json:stringify $request) "\\n\\n"))
     (((proc:queued $count)
       (let $reply (stream-test:frame $owner "")
         (let $printed (system:write stdout (str:concat (json:stringify $reply) "\\n")) $reply)))
      ($other (StreamTestFailure $other)))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/process-stream-tests")
    parser.add_argument("--repl", type=Path, help="an already built leanprover-community/repl executable")
    parser.add_argument("--lean", default="lean", help="Lean executable matching the REPL's build toolchain")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / "fixture"
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Werror",
                    str(ROOT / "tests/support/proc_fixture_v1.c"), "-o", str(fixture)], check=True)
    count = 0

    def run(name, body, *, raw=False):
        nonlocal count
        source = output / f"{name}.metta"
        source.write_text(IMPORTS + body + "\n")
        result = subprocess.run([str(binary), "--lang", "petta"] +
                                (["--no-results"] if raw else []) + [str(source)],
                                cwd=ROOT, capture_output=True, timeout=30)
        (output / f"{name}.stdout").write_bytes(result.stdout)
        (output / f"{name}.stderr").write_bytes(result.stderr)
        assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
        count += 1
        return result

    def has(name, body, text):
        result = run(name, body)
        assert text.encode() in result.stdout, (name, text, result.stdout, result.stderr)
        return result

    options = "((mode stream) (max-bytes 64) (timeout-ms 5000) (lifetime owner))"
    has("inherited-input-is-not-a-write-queue", '''!(let* (($p
        (proc:spawn ("/bin/sleep" "30") ((mode stream) (max-bytes 64)
          (stdin inherit) (stdout null) (stderr null) (group caller))))
        ($write (proc:write $p "cannot queue to inherited stdin"))
        ($out (proc:read $p stdout 0)) ($err (proc:read $p stderr 0))
        ($cancel (proc:cancel $p)) ($done (proc:await $p))
        ($closed (proc:close $p))) (MergedStreams $write $out $err))''',
        '(MergedStreams (proc:failed proc:input-closed "process input is closed") proc:eof proc:eof)')
    result = has("persistent-echo", f'''!(let* (($p (proc:spawn ("/bin/cat") {options}))
        ($w1 (proc:write $p "first")) ($r1 (stream-test:bytes $p stdout 5 ""))
        ($w2 (proc:write $p "next")) ($r2 (stream-test:bytes $p stdout 4 ""))
        ($end (proc:close-input $p)) ($eof (proc:read $p stdout 1000))
        ($done (proc:await $p)) ($closed (proc:close $p)))
        (Exchange $w1 $r1 $w2 $r2 $eof $done))''',
        '(Exchange (proc:queued 5) "first" (proc:queued 4) "next" proc:eof')
    assert b"(proc:stream-capture 0 9 9 0 0 0 proc:eof proc:eof (proc:input 9 9))" in result.stdout, result
    has("independent-streams", f'''!(let* (($a (proc:spawn ("/bin/cat") {options}))
        ($b (proc:spawn ("/bin/cat") {options}))
        ($wa (proc:write $a "a")) ($wb (proc:write $b "b"))
        ($rb (stream-test:bytes $b stdout 1 "")) ($ra (stream-test:bytes $a stdout 1 ""))
        ($ca (proc:close $a)) ($cb (proc:close $b))) (Separate $ra $rb))''', '(Separate "a" "b")')
    result = run("binary-backpressure", f'''!(let* (($p (proc:spawn ({json.dumps(str(fixture))} "bytes")
        ((mode stream) (max-bytes 7) (timeout-ms 5000) (lifetime owner))))
        ($out (stream-test:bytes $p stdout 256 ""))
        ($err (stream-test:bytes $p stderr 256 ""))
        ($out-eof (proc:read $p stdout 1000)) ($err-eof (proc:read $p stderr 1000))
        ($done (proc:await $p)) ($closed (proc:close $p))
        ($written (system:write stdout $out))) (system:write stderr $err))''', raw=True)
    assert result.stdout == bytes(range(256)) and result.stderr == bytes(range(256)), result
    has("pending-and-close", f'''!(let* (($p (proc:spawn ("/bin/cat") {options}))
        ($pending (proc:read $p stdout 0)) ($end (proc:close-input $p))
        ($bad (proc:write $p "x")) ($eof (proc:read $p stdout 1000))
        ($done (proc:await $p)) ($closed (proc:close $p))) (Observed $pending $bad $eof))''',
        '(Observed proc:pending (proc:failed proc:input-closed')
    has("missing-bound", '!(proc:spawn ("/bin/cat") ((mode stream)))', "proc:invalid-argument")
    has("stream-run-refused", f'!(proc:run ("/bin/cat") {options})', "proc:invalid-mode")
    has("invalid-mode", '!(proc:spawn ("/bin/cat") ((mode unknown)))', "proc:invalid-argument")
    has("oversize-initial", '!(proc:spawn ("/bin/cat") ((mode stream) (max-bytes 2) (stdin "abc")))',
        "proc:invalid-argument")
    has("oversize-write", f'''!(let* (($p (proc:spawn ("/bin/cat") {options}))
        ($bad (proc:write $p "{'x' * 65}")) ($closed (proc:close $p))) $bad)''', "proc:capacity")
    has("invalid-read", f'''!(let* (($p (proc:spawn ("/bin/cat") {options}))
        ($bad (proc:read $p elsewhere 0)) ($closed (proc:close $p))) $bad)''', "proc:invalid-argument")
    has("capture-mode", '''!(let* (($p (proc:spawn ("/bin/cat") ()))
        ($bad (proc:read $p stdout 0)) ($closed (proc:close $p))) $bad)''', "proc:invalid-mode")
    has("closed-owner", f'''!(let* (($p (proc:spawn ("/bin/cat") {options}))
        ($closed (proc:close $p))) (proc:read $p stdout 0))''', "proc:invalid-handle")
    reader, writer = os.pipe()
    try:
        capacity = fcntl.fcntl(writer, fcntl.F_GETPIPE_SZ) * 2
    finally:
        os.close(reader)
        os.close(writer)
    payload = json.dumps("x" * capacity)
    result = has("input-backpressure", f'''!(let* (($p (proc:spawn ({json.dumps(str(fixture))} "sleep")
        ((mode stream) (max-bytes {capacity}) (stdin {payload}) (timeout-ms 5000))))
        ($pressure (proc:write $p {payload})) ($cancel (proc:cancel $p))
        ($done (proc:await $p)) ($closed (proc:close $p))) (Observed $pressure $done))''',
        "(Observed proc:backpressure")
    assert b"proc:cancelled" in result.stdout, result
    has("stream-timeout", '''!(let* (($p (proc:spawn ("/bin/cat")
        ((mode stream) (max-bytes 8) (timeout-ms 30))))
        ($done (proc:await $p)) ($closed (proc:close $p))) $done)''', "proc:timed-out")
    has("unread-close", f'''!(let* (($p (proc:spawn ({json.dumps(str(fixture))} "bytes")
        ((mode stream) (max-bytes 1) (lifetime owner))))
        ($first (proc:read $p stderr 1000)) ($closed (proc:close $p))) StreamClosed)''', "StreamClosed")
    run("owner-abandon", f'!(proc:spawn ("/bin/cat") {options})')

    if args.repl:
        repl = args.repl.resolve()
        assert repl.is_file() and os.access(repl, os.X_OK), repl
        sysroot = subprocess.check_output([args.lean, "--print-prefix"], text=True).strip()
        environment = f'(("LEAN_SYSROOT" {json.dumps(sysroot)}) ("PATH" {json.dumps(os.defpath)}))'
        result = run("lean-repl", f'''!(let* (($p (proc:spawn ({json.dumps(str(repl))})
          ((mode stream) (max-bytes 1048576) (timeout-ms 15000) (lifetime owner) (env {environment}))))
          ($base (stream-test:request $p (JsonObject ((JsonPair "cmd" "import Lean\\ndef clientSeed : Nat := 37")))))
          ($env (json:get $base "env"))
          ($value (stream-test:request $p (JsonObject ((JsonPair "cmd" "#eval clientSeed + 5") (JsonPair "env" $env)))))
          ($bad (stream-test:request $p (JsonObject ((JsonPair "cmd" "example : clientSeed = 38 := by rfl") (JsonPair "env" $env)))))
          ($good (stream-test:request $p (JsonObject ((JsonPair "cmd" "theorem checkedSeed : clientSeed = 37 := rfl") (JsonPair "env" $env)))))
          ($rollback (stream-test:request $p (JsonObject ((JsonPair "cmd" "#check checkedSeed") (JsonPair "env" $env)))))
          ($kept (stream-test:request $p (JsonObject ((JsonPair "cmd" "#check checkedSeed") (JsonPair "env" (json:get $good "env"))))))
          ($proof (stream-test:request $p (JsonObject ((JsonPair "cmd" "example : True ∧ True := by exact And.intro True.intro True.intro")
             (JsonPair "allTactics" True) (JsonPair "env" $env)))))
          ($first (index-atom (index-atom (json:get $proof "tactics") 1) 0))
          ($split (stream-test:request $p (JsonObject ((JsonPair "tactic" "constructor") (JsonPair "proofState" (json:get $first "proofState"))))))
          ($finish (stream-test:request $p (JsonObject ((JsonPair "tactic" "all_goals trivial") (JsonPair "proofState" (json:get $split "proofState"))))))
          ($end (proc:close-input $p)) ($done (proc:await $p)) ($closed (proc:close $p))) ())''', raw=True)
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(replies) == 9, (replies, result.stderr)
        def errors(reply):
            return [m for m in reply.get("messages", []) if m.get("severity") == "error"]
        assert all(isinstance(row.get("env"), int) for row in replies[:7]), replies
        assert any(m.get("data") == "42" for m in replies[1].get("messages", [])), replies[1]
        assert errors(replies[2]) and errors(replies[4]), replies
        assert all(not errors(replies[i]) for i in (0, 1, 3, 5, 6)), replies
        assert len(replies[7].get("goals", [])) == 2 and replies[8].get("goals") == [], replies[7:]
        assert not any(row.get("sorries") for row in replies), replies
    print(f"(ProcessStreamSummary {count} 0)")


if __name__ == "__main__":
    main()

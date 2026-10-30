"""Fresh-process MeTTa queries over an owning native disk Space."""
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())
imports = "!(import! &self (library store))\n!(import! &self pat)\n"


def program(root, name, source):
    path = root / (name + ".metta")
    # Test directories are ordinary generated paths, never guest source.
    path.write_text(imports + source.replace("STORE_PATH", str(root / "store")))
    return path


def run(path, reference=False, expect=0):
    environment = os.environ.copy()
    if reference:
        environment["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    result = subprocess.run([binary, "--lang", "petta", str(path)],
                            stdin=subprocess.DEVNULL, capture_output=True,
                            text=True, timeout=30, env=environment)
    if expect is not None:
        assert result.returncode == expect, (result.returncode, result.stdout, result.stderr)
    assert not any(line == "ACCIDENTAL_EXECUTION" for line in result.stdout.splitlines())
    return result


writer = '''
!(import! &self (library system))
!(let* (((StoreOpened $o) (store:open "STORE_PATH" "create"))
 ((StoreSnapshot $v $e $r) (store:snapshot $o))
 ((StoreEncoded $a) (store:encode (edge alice bob)))
 ((StoreEncoded $b) (store:encode (rel $x $x)))
 ((StoreEncoded $rule) (store:encode (quote (= (saved-program) (println! ACCIDENTAL_EXECUTION)))))
 ((StoreEncoded $call) (store:encode (quote (saved-program))))
 ((StoreEncoded $m) (store:encode (StoreSpaceV1 "facts/" 5)))
 ((StoreSnapshot $w $ee $rr) (store:commit $o $v
   (ops (put "facts/a" $a) (put "facts/b" $a) (put "facts/c" $b)
        (put "facts/r" $rule) (put "facts/v" $call) (put "metadata" $m)
        (put "receipt/seed" "seed-v1")))))
 (let $_ (println! DURABLE_ACK) (system:read-line 600000)))
'''

reader = '''
(= (saved-program) (println! ACCIDENTAL_EXECUTION))
(= (all-from-space $s) (get-atoms $s))
(= (selected-space $s $events) (let $_ (add-atom $events selected) $s))
!(let* (((StoreOpened $o) (store:open "STORE_PATH" "existing"))
 ((StoreSnapshot $v $e $r) (store:snapshot $o))
 ($_ (assertEqual (store:get $v "receipt/seed" 32) (StoreValue "seed-v1")))
 ((StoreSpace $s $se $sr) (store:space-view $v "metadata"))
 ($_ (store:close $v)) ($_ (store:close $o))
 ($_ (assertEqual (store:space-view $v "metadata") (StoreRefused closed-view)))
 ($_ (assertEqual (collapse (match $s (edge alice $who) $who)) (bob bob)))
 ($_ (assertEqual (collapse (match $s (rel 1 2) no)) ()))
 ($_ (assertEqual (collapse (match $s (rel 1 1) yes)) (yes)))
 ($_ (assertEqual (size-atom (collapse (match $s $row $row))) 5))
 ($_ (assertEqual (size-atom (collapse (get-atoms $s))) 5))
 ($_ (assertEqual (size-atom (collapse (all-from-space $s))) 5))
 ($_ (assertEqual (size-atom (collapse (once (get-atoms $s)))) 1))
 ($events (new-space))
 ($_ (add-atom $events (known bob)))
 ($_ (assertEqual (collapse (match $s (edge alice $who) (match $events (known $who) $who))) (bob bob)))
 ($_ (assertEqual (size-atom (collapse (get-atoms (selected-space $s $events)))) 5))
 ($_ (assertEqual (collapse (match $events selected selected)) (selected)))
 ($_ (assertEqual (store:encode $s) (StoreCodecRefused "unsupported")))
 ($_ (assertEqual (collapse (match $s (= (saved-program) $body) found)) (found)))
 ($_ (assertEqual (collapse (match $s (saved-program) found)) (found)))
 ($_ (assertEqual (foldall + (match $s (edge alice $who) 1) 0) 2))
 ($_ (assertEqual (size-atom (collapse (match $s (, (edge alice $x) (edge alice $y)) ($x $y)))) 4))
 ($_ (assertEqual (catch (add-atom $s (edge alice carol)) (Error $call ReadOnlySpace) refused) refused))
 ($_ (assertEqual (catch (remove-atom $s (edge alice bob)) (Error $call ReadOnlySpace) refused) refused)))
 (collapse (let (pat:hit ((pat:row (pat:occurrence $instance $revision $identity) $original $instantiated)) $w)
                 (pat:query $s match% (edge alice $who))
                 (println! (IDENTITY $identity)))))
!(println! PUBLIC_SPACE_REOPEN_OK)
'''

with tempfile.TemporaryDirectory(prefix="cetta-persistent-space-", dir=os.environ.get("TMPDIR")) as folder:
    root = Path(folder)
    refused = root / "language-refusal.metta"
    refused.write_text('!(import! &self store)\n!(store:space-view not-a-live-view "metadata")\n')
    for language in ("he", "prime"):
        result = subprocess.run([binary, "--lang", language, str(refused)],
                                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and "(StoreRefused space-language)" in result.stdout, result
    (root / "store").mkdir(mode=0o700)
    writer_path = program(root, "write", writer)
    process = subprocess.Popen([binary, "--lang", "petta", str(writer_path)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        assert select.select([process.stdout], [], [], 30)[0], "writer did not acknowledge"
        assert process.stdout.readline().strip() == "DURABLE_ACK"
    finally:
        process.kill()
        process.communicate(timeout=10)
    assert process.returncode == -signal.SIGKILL

    identities = []
    for index, reference in enumerate((False, True)):
        warmup = "!(quote (" + " ".join("restart-symbol-" + str(i) for i in range(128 * (index + 1))) + "))\n"
        result = run(program(root, "read-" + str(index), warmup + reader), reference=reference)
        assert "PUBLIC_SPACE_REOPEN_OK" in result.stdout
        rows = [line for line in result.stdout.splitlines() if line.startswith("(IDENTITY ")]
        assert len(rows) == 2 and len(set(rows)) == 2, result.stdout
        assert all(re.fullmatch(r'\(IDENTITY \(StoreOccurrenceV1 "[0-9a-f]{32}" "facts/[ab]"\)\)', line) for line in rows)
        identities.append(rows)
    assert identities[0] == identities[1]

    # A valid byte-store root can still contain an invalid atom collection.
    # Neither a bad atom nor an incorrect count is a completed empty bag.
    for index, operations in enumerate((
        '(put "facts/v" "not-CAV1")',
        '(put "facts/v" $call) (put "metadata" $too-few)',
        '(put "metadata" $too-many)',
        '(put "metadata" $zero)',
    )):
        damaged = '''
!(let* (((StoreOpened $o) (store:open "STORE_PATH" "existing"))
 ((StoreSnapshot $v $e $r) (store:snapshot $o))
 ((StoreEncoded $call) (store:encode (quote (saved-program))))
 ((StoreEncoded $too-few) (store:encode (StoreSpaceV1 "facts/" 4)))
 ((StoreEncoded $too-many) (store:encode (StoreSpaceV1 "facts/" 6)))
 ((StoreEncoded $zero) (store:encode (StoreSpaceV1 "facts/" 0)))
 ((StoreSnapshot $w $ee $rr) (store:commit $o $v (ops OPERATIONS)))
 ((StoreSpace $s $se $sr) (store:space-view $w "metadata")))
 (collapse (match $s (edge alice $who) $who)))
'''.replace("OPERATIONS", operations)
        result = run(program(root, "damaged-" + str(index), damaged), expect=None)
        assert result.returncode != 0 and "observation incomplete: host-failure" in result.stderr, result
        assert "(bob bob)" not in result.stdout and "()" not in result.stdout, result

    restore = '''
!(let* (((StoreOpened $o) (store:open "STORE_PATH" "existing"))
 ((StoreSnapshot $v $e $r) (store:snapshot $o))
 ((StoreEncoded $m) (store:encode (StoreSpaceV1 "facts/" 5)))
 ((StoreSnapshot $w $ee $rr) (store:commit $o $v (ops (put "metadata" $m)))))
 (println! RESTORED))
'''
    assert "RESTORED" in run(program(root, "restore", restore)).stdout
    assert "PUBLIC_SPACE_REOPEN_OK" in run(program(root, "read-restored", reader)).stdout

    malformed = '''
!(let* (((StoreOpened $o) (store:open "STORE_PATH" "existing"))
 ((StoreSnapshot $v $e $r) (store:snapshot $o))
 ((StoreEncoded $bad) (store:encode (StoreSpaceV1 "facts/" -1)))
 ((StoreEncoded $overlap) (store:encode (StoreSpaceV1 "facts/" 5)))
 ((StoreSnapshot $w $ee $rr) (store:commit $o $v
   (ops (put "bad-metadata" $bad) (put "facts/metadata" $overlap)))))
 (let* (($_ (assertEqual (store:space-view $w "absent") (StoreRefused absent)))
        ($_ (assertEqual (store:space-view $w "bad-metadata") (StoreRefused space-metadata-format)))
        ($_ (assertEqual (store:space-view $w "facts/metadata") (StoreRefused space-metadata-format))))
  (println! METADATA_REFUSAL_OK)))
'''
    assert "METADATA_REFUSAL_OK" in run(program(root, "metadata", malformed)).stdout

print("PASS: public MeTTa Space survives acknowledged writer kill; ordinary matching, counts, joins, inert programs, read-only guards and occurrence identities agree with the reference matcher")

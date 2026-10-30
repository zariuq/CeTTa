"""MeTTa collection edits, ordinary matching and durable retry receipts."""
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())
header = "!(import! &self (library durable_space))\n!(import! &self (library system))\n"


def sequence(bindings, body):
    return "(let* (" + "\n".join("(" + key + " " + value + ")" for key, value in bindings) + ")\n" + body + ")"


def check(expression, expected):
    return ("$_", "(assertEqual " + expression + " " + expected + ")")


def write(root, name, expression):
    path = root / (name + ".metta")
    path.write_text(header + "!" + expression.replace("STORE_PATH", str(root / "store")) + "\n")
    return path


def run(path):
    result = subprocess.run([binary, "--lang", "petta", str(path)],
                            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "ACCIDENTAL_EXECUTION\n" not in result.stdout, result.stdout
    return result.stdout


open_store = [
    ("(StoreOpened $o)", '(store:open "STORE_PATH" "existing")'),
    ("(StoreSnapshot $v $epoch $revision)", "(store:snapshot $o)"),
]
changes = "((SpaceInsertV1 a (edge alice bob)) (SpaceInsertV1 b (edge alice bob)))"

with tempfile.TemporaryDirectory(prefix="cetta-space-transactions-", dir=os.environ.get("TMPDIR")) as folder:
    root = Path(folder)
    (root / "store").mkdir(mode=0o700)
    seed = sequence([
        ("(StoreOpened $o)", '(store:open "STORE_PATH" "create")'),
        ("(StoreSnapshot $v $e $r)", "(store:snapshot $o)"),
        ("(KnowledgeCommittedV1 (SpaceCreatedV1 sample) $e1 $r1)",
         "(durable-space:create $o $v sample create-one)"),
        ("(StoreSnapshot $w $e2 $r2)", "(store:snapshot $o)"),
        ("(StoreSpace $empty $ee $er)", "(durable-space:capture $w sample)"),
        check("(collapse (get-atoms $empty))", "()"),
        ("(KnowledgeCommittedV1 (SpaceAppliedV1 ($a $b)) $e3 $r3)",
         "(durable-space:commit $o $w sample insert-one " + changes + ")"),
    ], "(let $_ (println! COLLECTION_ACK) (system:read-line 600000))")
    process = subprocess.Popen([binary, "--lang", "petta", str(write(root, "seed", seed))],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        assert select.select([process.stdout], [], [], 30)[0], "no collection acknowledgement"
        assert process.stdout.readline().strip() == "COLLECTION_ACK"
    finally:
        process.kill(); process.communicate(timeout=10)
    assert process.returncode == -signal.SIGKILL

    # Resolving the lost insertion reply recovers the original references;
    # replay neither advances the revision nor duplicates equal occurrences.
    bindings = open_store + [
        ("(KnowledgeResolvedV1 (SpaceAppliedV1 ($a $b)) $re $rr)",
         "(knowledge:resolve $o insert-one (SpaceCommitV1 sample " + changes + ") 65536)"),
        ("(StoreSnapshot $base $be $br)", "(store:snapshot $o)"),
        check("(durable-space:commit $o $base sample insert-one " + changes + ")", "(KnowledgeReplayedV1 (SpaceAppliedV1 ($a $b)))"),
        check("(durable-space:commit $o $base sample insert-one ((SpaceInsertV1 a changed)))", "(KnowledgeRefusedV1 receipt-collision)"),
        ("(StoreSpace $old $oe $or)", "(durable-space:capture $base sample)"),
        check("(collapse (match $old (edge alice $who) $who))", "(bob bob)"),
        check("(durable-space:commit $o $base sample duplicate ((SpaceInsertV1 c value) (SpaceInsertV1 c other)))", "(SpaceRefusedV1 repeated-occurrence)"),
        check("(durable-space:commit $o $base sample invalid-batch ((SpaceInsertV1 c value) invalid))", "(SpaceRefusedV1 edit-format)"),
        check("(durable-space:commit $o $base sample invalid-list $unbound)", "(SpaceRefusedV1 edit-list)"),
        check("(durable-space:commit $o $base sample invalid-identity ((SpaceInsertV1 $unbound value)))", "(KnowledgeRefusedV1 nonground-identity)"),
        ("(StoreSnapshot $unchanged $ue $ur)", "(store:snapshot $o)"),
        check("$ur", "$br"),
        ("(KnowledgeCommittedV1 (SpaceAppliedV1 ()) $ce $cr)",
         "(durable-space:commit $o $base sample update-delete ((SpaceUpdateV1 $a (edge alice carol)) (SpaceDeleteV1 $b)))"),
        ("(StoreSnapshot $updated $ne $nr)", "(store:snapshot $o)"),
        ("(StoreSpace $new $se $sr)", "(durable-space:capture $updated sample)"),
        check("(collapse (match $new (edge alice $who) $who))", "(carol)"),
        check("(collapse (match $old (edge alice $who) $who))", "(bob bob)"),
        # A deleted occurrence cannot be recreated by replaying its insertion.
        check("(durable-space:commit $o $updated sample insert-one " + changes + ")", "(KnowledgeReplayedV1 (SpaceAppliedV1 ($a $b)))"),
        check("(durable-space:commit $o $updated sample delete-again ((SpaceDeleteV1 $b)))", "(SpaceRefusedV1 occurrence-absent)"),
        ("(StoreOccurrenceV1 $ae $ak)", "$a"),
        check('(durable-space:commit $o $updated sample wrong-epoch ((SpaceDeleteV1 (StoreOccurrenceV1 "wrong-epoch" $ak))))', "(SpaceRefusedV1 occurrence-reference)"),
        check('(durable-space:commit $o $updated sample wrong-collection ((SpaceDeleteV1 (StoreOccurrenceV1 $ae "elsewhere"))))', "(SpaceRefusedV1 occurrence-collection)"),
        # Two publishers race from the same view. In particular the absent
        # match cannot authorize a write after the other publisher inserts it.
        check("(collapse (match $new (edge racer $who) $who))", "()"),
        ("(StoreOpened $other)", '(store:open "STORE_PATH" "existing")'),
        ("(KnowledgeCommittedV1 (SpaceAppliedV1 ($c)) $xe $xr)",
         "(durable-space:commit $other $updated sample winning ((SpaceInsertV1 c (edge racer joined))))"),
        check("(durable-space:commit $o $updated sample stale ((SpaceInsertV1 c (edge racer stale))))", "(StoreRefused conflict)"),
        ("(StoreSnapshot $latest $le $lr)", "(store:snapshot $o)"),
        ("(StoreSpace $live $lve $lvr)", "(durable-space:capture $latest sample)"),
        check("(collapse (match $live (edge racer $who) $who))", "(joined)"),
        check("(size-atom (collapse (match $live $row $row)))", "2"),
        check("(knowledge:receipt $latest stale (SpaceCommitV1 sample ((SpaceInsertV1 c (edge racer stale)))) 65536)", "KnowledgeReceiptAbsentV1"),
        # Insert callable syntax as a value; neither staging nor querying it
        # runs the payload. Its receipt stores that same syntax.
        ("(KnowledgeCommittedV1 (SpaceAppliedV1 ($d)) $de $dr)",
         "(durable-space:commit $o $latest sample inert ((SpaceInsertV1 d (println! ACCIDENTAL_EXECUTION))))"),
        ("(StoreSnapshot $final $fe $fr)", "(store:snapshot $o)"),
        ("(StoreSpace $final-space $fse $fsr)", "(durable-space:capture $final sample)"),
        check("(size-atom (collapse (match $final-space (println! ACCIDENTAL_EXECUTION) found)))", "1"),
        check("(size-atom (collapse (match $final-space $row $row)))", "3"),
        check('(durable-space:commit $o $final sample capability ((SpaceInsertV1 cap $final-space)))', '(StoreCodecRefused "unsupported")'),
        ("(KnowledgeCommittedV1 (SpaceCreatedV1 other) $oce $ocr)",
         "(durable-space:create $o $final other create-other)"),
        ("(StoreSnapshot $other-view $ove $ovr)", "(store:snapshot $o)"),
        ("(KnowledgeCommittedV1 (SpaceAppliedV1 ($open)) $ope $opr)",
         "(durable-space:commit $o $other-view other open-row ((SpaceInsertV1 open (rel $x $x))))"),
        ("(StoreSnapshot $open-view $oe2 $or2)", "(store:snapshot $o)"),
        ("(StoreSpace $open-space $ose $osr)", "(durable-space:capture $open-view other)"),
        check("(collapse (match $open-space (rel 1 2) bad))", "()"),
        check("(collapse (match $open-space (rel 1 1) good))", "(good)"),
        check("(durable-space:commit $o $open-view other cross-collection ((SpaceDeleteV1 $a)))", "(SpaceRefusedV1 occurrence-collection)"),
        # An orphaned row is not silently adopted as an empty collection.
        ("(KnowledgeAddressV1 $prefix)", "(durable-space:prefix orphan)"),
        ("$orphan-key", '(str:concat $prefix "orphan")'),
        ("(StoreEncoded $orphan-body)", "(store:encode orphan)"),
        ("(StoreSnapshot $orphan-view $ore $orr)",
         "(store:commit $o $open-view (ops (put $orphan-key $orphan-body)))"),
        check("(durable-space:create $o $orphan-view orphan create-orphan)", "(SpaceRefusedV1 orphaned-occurrences)"),
    ]
    output = run(write(root, "transactions", sequence(bindings, "(println! SPACE_TRANSACTIONS_OK)")))
    assert "SPACE_TRANSACTIONS_OK" in output, output

    # A separate process sees exactly the final committed rows and resolves
    # the same receipt. Querying needs no application-specific row decoder.
    final = sequence(open_store + [
        ("(StoreSpace $s $se $sr)", "(durable-space:capture $v sample)"),
        check("(collapse (match $s (edge alice $who) $who))", "(carol)"),
        check("(collapse (match $s (edge racer $who) $who))", "(joined)"),
        check("(size-atom (collapse (match $s $row $row)))", "3"),
    ], "(println! SPACE_RESTART_OK)")
    output = run(write(root, "final", final))
    assert "SPACE_RESTART_OK" in output, output

print("PASS: durable atom collections preserve receipts, atomic count/data changes, occurrence references, captured reads and whole-revision conflicts across processes")

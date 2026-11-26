#!/usr/bin/env python3
"""Cross-process capability sealing & carrier-independent capability-algebra tests.

Verifies two layers across both HE and PeTTa:

1. Carrier-independent capability-algebra law suite:
   Runs identical capability laws through a pluggable `CapabilityCarrier`
   adapter interface over both:
     - `InProcessFacetCarrier` (`lib/seal` opaque write facets), and
     - `MacaroonV1Carrier` (`macaroon-hmac-sha256` v1 wire tokens).
   Future carriers (Unix FD passing, Biscuit/UCAN public-key tokens, rho
   unforgeable names, ZK credentials) plug into the exact same law suite.

2. Real two-CeTTa-process IPC protocol verification:
   Launches two concurrent `cetta` OS processes synchronized over POSIX FIFOs
   with zero shared memory:
     - Process A (Authority) seals head `ledger`, holds its ephemeral 256-bit
       CSPRNG secret strictly in RAM, mints a wire root token and single-use
       challenge nonce, and publishes them over the wire.
     - Process B (Worker) receives only the wire token and nonce, derives
       attenuated and tampered tokens completely offline (without any access to
       Process A's secret), and sends them back to Process A.
     - Process A verifies every wire token against its live in-memory authority
       and sealed space: accepting valid attenuated writes, rejecting replay,
       widening, expiry, stripped caveats, flipped signatures, and revoking the
       root together with every descendant.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from abc import ABC, abstractmethod
from pathlib import Path


def run_case(
    cetta: Path, work_dir: Path, files: dict[str, str], flags: list[str],
    entry: str = "main.metta",
) -> tuple[int, list[str], str]:
    case_dir = Path(tempfile.mkdtemp(dir=work_dir))
    try:
        for rel, content in files.items():
            target = case_dir / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")
        proc = subprocess.run(
            [str(cetta), *flags, str(case_dir / entry)],
            cwd=case_dir,
            capture_output=True,
            text=True,
            timeout=30,
        )
        lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
        return proc.returncode, lines, proc.stderr.strip()
    finally:
        shutil.rmtree(case_dir, ignore_errors=True)


def strip_brackets(line: str, is_he: bool) -> str:
    if is_he and line.startswith("[") and line.endswith("]"):
        return line[1:-1]
    return line


def check_eq(label: str, got, expected) -> None:
    if got != expected:
        raise AssertionError(f"{label}:\n  expected: {expected!r}\n  got:      {got!r}")


_CASE_SEQ = 0


def try_case(expr: str, tag: str) -> str:
    global _CASE_SEQ
    _CASE_SEQ += 1
    k = _CASE_SEQ
    return (
        f"(case {expr}\n"
        f"   (((Error $ec{k} (SealFault $ewhy{k})) ({tag} fault $ewhy{k}))\n"
        f"    ($eok{k} ({tag} ok))))"
    )


def dialect_catch_add(is_he: bool, expr: str, tag: str) -> str:
    if is_he:
        return f"!{try_case(expr, tag)}"
    return f"!{try_case(f'(catch {expr})', tag)}"


# ── Part 1: Carrier-independent capability-algebra law suite ─────────────────

class CapabilityCarrier(ABC):
    """Adapter interface that every head-sealing capability carrier satisfies."""

    name: str

    @abstractmethod
    def setup_snippet(self, head: str) -> str:
        """MeTTa setup binding &cap-root (and any carrier authority state) after
        sealing HEAD on &self."""

    @abstractmethod
    def try_add_expr(self, cred: str, atom: str) -> str:
        """MeTTa expression attempting authorized add of ATOM via CRED."""

    @abstractmethod
    def try_remove_expr(self, cred: str, atom: str) -> str:
        """MeTTa expression attempting authorized remove of ATOM via CRED."""

    @abstractmethod
    def attenuate_drop_remove_expr(self, cred: str) -> str:
        """MeTTa expression attenuating CRED so remove is no longer permitted."""

    @abstractmethod
    def attenuate_drop_all_expr(self, cred: str) -> str:
        """MeTTa expression attenuating CRED so neither add nor remove works."""

    @abstractmethod
    def wrong_authority_cred_expr(self) -> str:
        """MeTTa expression producing a credential from an unrelated authority."""

    @abstractmethod
    def expected_wrong_authority_fault(self) -> str:
        """Expected SealFault reason when presenting an unrelated credential."""


class InProcessFacetCarrier(CapabilityCarrier):
    name = "in-process-facet"

    def setup_snippet(self, head: str) -> str:
        return (
            f"!(let $w (seal:seal-head &self {head} {head}-brand)\n"
            f"   (bind! &cap-root (new-state $w)))"
        )

    def try_add_expr(self, cred: str, atom: str) -> str:
        return f"(seal:try-head-add {cred} &self {atom})"

    def try_remove_expr(self, cred: str, atom: str) -> str:
        return f"(seal:try-head-remove {cred} &self {atom})"

    def attenuate_drop_remove_expr(self, cred: str) -> str:
        return f"(seal:attenuate {cred} none)"

    def attenuate_drop_all_expr(self, cred: str) -> str:
        return f"(seal:attenuate {cred} none)"

    def wrong_authority_cred_expr(self) -> str:
        return "(let (seal:facets $s $u) (seal:new other-brand) $s)"

    def expected_wrong_authority_fault(self) -> str:
        return "wrong-facet"


class MacaroonV1Carrier(CapabilityCarrier):
    name = "macaroon-hmac-sha256-v1"

    def setup_snippet(self, head: str) -> str:
        return (
            f"!(let $w (seal:seal-head &self {head} {head}-brand)\n"
            f"   (let $a (seal:new-token-authority $w {head}-auth)\n"
            f"     (let $_ (bind! &cap-auth (new-state $a))\n"
            f"       (bind! &cap-root (new-state (seal:token-mint $a))))))"
        )

    def try_add_expr(self, cred: str, atom: str) -> str:
        return f"(seal:try-token-head-add (get-state &cap-auth) {cred} &self {atom})"

    def try_remove_expr(self, cred: str, atom: str) -> str:
        return f"(seal:try-token-head-remove (get-state &cap-auth) {cred} &self {atom})"

    def attenuate_drop_remove_expr(self, cred: str) -> str:
        return f"(seal:token-attenuate {cred} (op add))"

    def attenuate_drop_all_expr(self, cred: str) -> str:
        return f"(seal:token-attenuate (seal:token-attenuate {cred} (op add)) (op remove))"

    def wrong_authority_cred_expr(self) -> str:
        return (
            "(let (seal:facets $s $u) (seal:new other-brand)\n"
            "  (let $a2 (seal:new-token-authority $s other-auth)\n"
            "    (seal:token-mint $a2)))"
        )

    def expected_wrong_authority_fault(self) -> str:
        return "forged-proof"


def run_carrier_algebra_laws(
    cetta: Path, work: Path, label: str, flags: list[str], is_he: bool,
    carrier: CapabilityCarrier,
) -> None:
    """Run identical capability-algebra laws over CARRIER."""
    prog = "\n".join([
        "!(import! &self seal)",
        "(= (vault 1) init)",
        carrier.setup_snippet("vault"),
        # Law 1: unauthenticated write refused
        dialect_catch_add(is_he, "(add-atom &self (= (vault 2) hijack))", "unauth"),
        # Law 2: root credential can add and remove
        f"!(let $r (get-state &cap-root) {try_case(carrier.try_add_expr('$r', '(= (vault 2) ok-val)'), 'root-add')})",
        "!(vault 2)",
        f"!(let $r (get-state &cap-root) {try_case(carrier.try_remove_expr('$r', '(= (vault 2) ok-val)'), 'root-rem')})",
        # Law 3: attenuated credential rejects dropped operation
        f"!(let $r (get-state &cap-root) (let $d {carrier.attenuate_drop_remove_expr('$r')} {try_case(carrier.try_remove_expr('$d', '(= (vault 1) init)'), 'drop-rem')}))",
        # Law 4: fully attenuated credential rejects both add and remove
        f"!(let $r (get-state &cap-root) (let $z {carrier.attenuate_drop_all_expr('$r')} {try_case(carrier.try_add_expr('$z', '(= (vault 3) nope)'), 'drop-all')}))",
        # Law 5: unrelated authority credential rejected
        f"!(let $bad {carrier.wrong_authority_cred_expr()} {try_case(carrier.try_add_expr('$bad', '(= (vault 4) bad)'), 'other-auth')})",
        # Law 6: original state preserved
        "!(vault 1)",
    ]) + "\n"

    rc, lines, err = run_case(cetta, work, {"main.metta": prog}, flags)
    check_eq(f"{label}/{carrier.name} rc", (rc, err), (0, ""))
    tail = [strip_brackets(ln, is_he) for ln in lines[-8:]]
    check_eq(
        f"{label}/{carrier.name} algebra laws",
        tail,
        [
            "(unauth fault head-sealed)",
            "(root-add ok)",
            "ok-val",
            "(root-rem ok)",
            "(drop-rem fault missing-right)",
            "(drop-all fault missing-right)",
            f"(other-auth fault {carrier.expected_wrong_authority_fault()})",
            "init",
        ],
    )


def run_macaroon_v1_specific_laws(
    cetta: Path, work: Path, label: str, flags: list[str], is_he: bool,
) -> None:
    """Verify macaroon-hmac-sha256 v1 wire portability, monotonic caveats,
    single-use nonces, expiry, root revocation, and fail-closed open format."""
    prog = "\n".join([
        "!(import! &self seal)",
        "(= (vault 1) init)",
        "!(let $w (seal:seal-head &self vault vault-brand)",
        "   (let $a (seal:new-token-authority $w vault-auth)",
        "     (bind! &auth (new-state $a))))",
        "!(let $a (get-state &auth)",
        "   (let* (($r1 (seal:token-mint $a))",
        "          ($r2 (seal:token-mint $a))",
        "          ($p (seal:portable $r1))",
        "          ($now (seal:now-ms))",
        "          ($future (+ $now 60000))",
        "          ($past (- $now 60000))",
        "          ($n1 (seal:token-challenge $a))",
        "          ($t-head (seal:token-attenuate $r1 (head vault)))",
        "          ($t-wrong-head (seal:token-attenuate $t-head (head other)))",
        "          ($t-add (seal:token-attenuate $t-head (op add)))",
        "          ($t-widen (seal:token-attenuate $t-add (op write)))",
        "          ($t-exp-ok (seal:token-attenuate $t-add (expires $future)))",
        "          ($t-exp-bad (seal:token-attenuate $t-add (expires $past)))",
        "          ($t-exp-rescue (seal:token-attenuate $t-exp-bad (expires $future)))",
        "          ($t-nonce (seal:token-attenuate $t-exp-ok (nonce $n1)))",
        "          ($t-bad-nonce (seal:token-attenuate $t-add (nonce \"00112233445566778899aabbccddeeff\")))",
        "          ($c1 " + try_case("(seal:try-token-verify $a $t-wrong-head vault add)", "wrong-head") + ")",
        "          ($c2 " + try_case("(seal:try-token-verify $a $t-widen vault remove)", "widen-op") + ")",
        "          ($c3 " + try_case("(seal:try-token-verify $a $t-exp-bad vault add)", "expired") + ")",
        "          ($c4 " + try_case("(seal:try-token-verify $a $t-exp-rescue vault add)", "rescue") + ")",
        "          ($c5 " + try_case("(seal:try-token-verify $a $t-bad-nonce vault add)", "bad-nonce") + ")",
        "          ($c6 " + try_case("(seal:try-token-head-add $a $t-nonce &self (= (vault 2) once))", "nonce-first") + ")",
        "          ($c7 " + try_case("(seal:try-token-head-add $a $t-nonce &self (= (vault 3) twice))", "nonce-replay") + ")",
        "          ($_rev (seal:token-revoke $a $r1))",
        "          ($c8 " + try_case("(seal:try-token-verify $a $r1 vault add)", "rev-root") + ")",
        "          ($c9 " + try_case("(seal:try-token-verify $a $t-exp-ok vault add)", "rev-desc") + ")",
        "          ($c10 " + try_case("(seal:try-token-verify $a $r2 vault add)", "other-root") + ")",
        "          ($c11 " + try_case("(seal:try-token-verify $a (seal:token biscuit-ed25519 1 \"id\" (caveats) \"0000000000000000000000000000000000000000000000000000000000000000\") vault add)", "bad-carrier") + ")",
        "          ($c12 " + try_case("(seal:try-token-verify $a (seal:token macaroon-hmac-sha256 99 \"id\" (caveats) \"0000000000000000000000000000000000000000000000000000000000000000\") vault add)", "bad-ver") + ")",
        "          ($c13 " + try_case("(seal:try-token-attenuate $r2 \"op=add\")", "str-cav") + ")",
        "          ($c14 " + try_case("(seal:try-token-attenuate $r2 (custom foo))", "unk-cav") + "))",
        "     (results (== $p $r1) $c1 $c2 $c3 $c4 $c5 $c6 $c7 $c8 $c9 $c10 $c11 $c12 $c13 $c14)))",
        "!(vault 2)",
    ]) + "\n"

    rc, lines, err = run_case(cetta, work, {"main.metta": prog}, flags)
    check_eq(f"{label} macaroon-v1 laws rc", (rc, err), (0, ""))
    tail = [strip_brackets(ln, is_he) for ln in lines[-2:]]
    true_str = "True" if is_he else "true"
    check_eq(
        f"{label} macaroon-v1 laws",
        tail,
        [
            f"(results {true_str} "
            "(wrong-head fault wrong-head) "
            "(widen-op fault missing-right) "
            "(expired fault expired) "
            "(rescue fault expired) "
            "(bad-nonce fault invalid-nonce) "
            "(nonce-first ok) "
            "(nonce-replay fault nonce-replayed) "
            "(rev-root fault revoked) "
            "(rev-desc fault revoked) "
            "(other-root ok) "
            "(bad-carrier fault unsupported-carrier) "
            "(bad-ver fault unsupported-version) "
            "(str-cav fault unsupported-caveat) "
            "(unk-cav fault unsupported-caveat))",
            "once",
        ],
    )


# ── Part 2: Real two-CeTTa-process IPC test over POSIX FIFOs ─────────────────

def run_two_process_ipc(
    cetta: Path, work: Path, label: str, flags: list[str], is_he: bool,
) -> None:
    """Spawn two concurrent CeTTa OS processes (Authority + Worker) synchronized
    by POSIX FIFOs, exchanging only pure wire s-expressions."""
    case_dir = Path(tempfile.mkdtemp(dir=work))
    try:
        grant_fifo = case_dir / "grant_ready.fifo"
        reply_fifo = case_dir / "reply_ready.fifo"
        os.mkfifo(grant_fifo)
        os.mkfifo(reply_fifo)

        authority_code = "\n".join([
            "!(import! &self seal)",
            "!(import! &self fs)",
            "(= (ledger 1) genesis)",
            "!(let $w (seal:seal-head &self ledger ledger-brand)",
            "   (let $a (seal:new-token-authority $w ledger-auth)",
            "     (let* (($_1 (bind! &auth (new-state $a)))",
            "            ($root (seal:token-mint $a))",
            "            ($nonce (seal:token-challenge $a))",
            "            ($_2 (bind! &root-tok (new-state $root)))",
            "            ($grant-atom (wire-grant $root $nonce))",
            "            ($grant-text (repr $grant-atom))",
            "            ($_3 (fs:write \"grant.metta\" $grant-text))",
            "            ($_4 (fs:write \"grant_ready.fifo\" \"ready\"))",
            "            ($_5 (fs:read-text \"reply_ready.fifo\")))",
            "       ready-to-verify)))",
            "!(include \"requests.metta\")",
            "!(let $a (get-state &auth)",
            "   (let $root (get-state &root-tok)",
            "     (match &self (wire-requests $tok-ok $tok-add $tok-widen $tok-exp $tok-rescue $tok-strip $tok-flip $tok-desc)",
            "       (let* (($r1 " + try_case("(seal:try-token-head-add $a $tok-ok &self (= (ledger 2) from-worker))", "ok") + ")",
            "              ($r2 " + try_case("(seal:try-token-head-add $a $tok-ok &self (= (ledger 9) replay))", "replay") + ")",
            "              ($r3 " + try_case("(seal:try-token-head-remove $a $tok-add &self (= (ledger 1) genesis))", "add-rem") + ")",
            "              ($r4 " + try_case("(seal:try-token-head-remove $a $tok-widen &self (= (ledger 1) genesis))", "widen") + ")",
            "              ($r5 " + try_case("(seal:try-token-head-add $a $tok-exp &self (= (ledger 9) expired-val))", "expired-tok") + ")",
            "              ($r6 " + try_case("(seal:try-token-head-add $a $tok-rescue &self (= (ledger 9) rescue))", "rescue") + ")",
            "              ($r7 " + try_case("(seal:try-token-head-add $a $tok-strip &self (= (ledger 9) strip))", "strip") + ")",
            "              ($r8 " + try_case("(seal:try-token-head-add $a $tok-flip &self (= (ledger 9) flip))", "flip") + ")",
            "              ($r9 " + try_case("(seal:try-token-head-add $a $tok-desc &self (= (ledger 3) pre-revoke))", "pre-rev") + ")",
            "              ($_rev (seal:token-revoke $a $root))",
            "              ($r10 " + try_case("(seal:try-token-head-add $a $tok-desc &self (= (ledger 4) post-rev-desc))", "post-rev-desc") + ")",
            "              ($r11 " + try_case("(seal:try-token-head-add $a $root &self (= (ledger 4) post-rev-root))", "post-rev-root") + "))",
            "         (ipc-results $r1 $r2 $r3 $r4 $r5 $r6 $r7 $r8 $r9 $r10 $r11)))))",
            "!(ledger 1)",
            "!(ledger 2)",
            "!(ledger 3)",
            "!(collapse (ledger 4))",
        ]) + "\n"

        worker_code = "\n".join([
            "!(import! &self seal)",
            "!(import! &self fs)",
            "!(fs:read-text \"grant_ready.fifo\")",
            "!(include \"grant.metta\")",
            "!(match &self (wire-grant $root $nonce)",
            "   (let* (($now (seal:now-ms))",
            "          ($future (+ $now 60000))",
            "          ($tok-desc (seal:token-attenuate $root (head ledger)))",
            "          ($tok-add (seal:token-attenuate $tok-desc (op add)))",
            "          ($tok-ok (seal:token-attenuate (seal:token-attenuate $tok-add (expires $future)) (nonce $nonce)))",
            "          ($tok-widen (seal:token-attenuate $tok-add (op remove)))",
            "          ($tok-exp (seal:token-attenuate $tok-desc (expires 1)))",
            "          ($tok-rescue (seal:token-attenuate $tok-exp (expires $future)))",
            "          ((seal:token $c $v $rid $cavs $sig) $tok-add)",
            "          ($tok-strip (seal:token $c $v $rid (caveats) $sig))",
            "          ($tok-flip (seal:token $c $v $rid $cavs \"0000000000000000000000000000000000000000000000000000000000000000\"))",
            "          ($req-atom (wire-requests $tok-ok $tok-add $tok-widen $tok-exp $tok-rescue $tok-strip $tok-flip $tok-desc))",
            "          ($req-text (repr $req-atom))",
            "          ($_1 (fs:write \"requests.metta\" $req-text))",
            "          ($_2 (fs:write \"reply_ready.fifo\" \"done\")))",
            "     worker-done))",
        ]) + "\n"

        (case_dir / "authority.metta").write_text(authority_code, encoding="utf-8")
        (case_dir / "worker.metta").write_text(worker_code, encoding="utf-8")

        proc_a = subprocess.Popen(
            [str(cetta), *flags, str(case_dir / "authority.metta")],
            cwd=case_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        proc_b = subprocess.Popen(
            [str(cetta), *flags, str(case_dir / "worker.metta")],
            cwd=case_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            out_b, err_b = proc_b.communicate(timeout=30)
            out_a, err_a = proc_a.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            proc_a.kill()
            proc_b.kill()
            raise AssertionError(f"{label} 2-process IPC timed out")

        check_eq(f"{label} worker rc", (proc_b.returncode, err_b.strip()), (0, ""))
        check_eq(f"{label} authority rc", (proc_a.returncode, err_a.strip()), (0, ""))

        lines_b = [strip_brackets(ln.strip(), is_he) for ln in out_b.splitlines() if ln.strip()]
        check_eq(f"{label} worker done", lines_b[-1], "worker-done")

        lines_a = [strip_brackets(ln.strip(), is_he) for ln in out_a.splitlines() if ln.strip()]
        check_eq(
            f"{label} 2-process IPC results",
            lines_a[-5:],
            [
                "(ipc-results "
                "(ok ok) "
                "(replay fault nonce-replayed) "
                "(add-rem fault missing-right) "
                "(widen fault missing-right) "
                "(expired-tok fault expired) "
                "(rescue fault expired) "
                "(strip fault forged-proof) "
                "(flip fault forged-proof) "
                "(pre-rev ok) "
                "(post-rev-desc fault revoked) "
                "(post-rev-root fault revoked))",
                "genesis",
                "from-worker",
                "pre-revoke",
                "((ledger 4))" if is_he else "()",
            ],
        )
    finally:
        shutil.rmtree(case_dir, ignore_errors=True)


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit("usage: test_space_head_sealing_cross_process_v1.py <path-to-cetta>")
    cetta = Path(sys.argv[1]).resolve()
    work = Path(tempfile.mkdtemp(prefix="cetta-head-sealing-ipc-"))
    carriers: list[CapabilityCarrier] = [
        InProcessFacetCarrier(),
        MacaroonV1Carrier(),
    ]
    dialects = [
        ("HE", ["--lang", "he"], True),
        ("PeTTa", ["--lang", "petta"], False),
    ]
    try:
        for label, flags, is_he in dialects:
            for carrier in carriers:
                run_carrier_algebra_laws(cetta, work, label, flags, is_he, carrier)
            run_macaroon_v1_specific_laws(cetta, work, label, flags, is_he)
            run_two_process_ipc(cetta, work, label, flags, is_he)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(
        "PASS: test_space_head_sealing_cross_process_v1 "
        "(HE + PeTTa: carrier-independent algebra laws over facets & macaroon-hmac-sha256 v1, "
        "monotonicity, nonces, expiry, revocation, fail-closed open format, real 2-process FIFO IPC)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

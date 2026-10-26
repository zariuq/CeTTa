#!/usr/bin/env python3

from __future__ import annotations

import argparse
import random
import subprocess
import sys

from rhocalc_m3_rholang_cli_compare import (
    contract_name_key,
    contract_proc_key,
)
from rhocalc_tiny_semantics import (
    ProcDrop,
    ProcNil,
    ProcPar,
    ProcRecv,
    ProcSend,
    parse_mrho_proc,
    top_level_components,
)


def run_rho(bin_path: str, expr: str, *, threads: int = 4, limit: int | None = None) -> tuple[int, str, str]:
    cmd = [
        bin_path,
        "--num-threads",
        str(threads),
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
    ]
    if limit is not None:
        cmd.extend(["--rho-reduction-limit", str(limit)])
    cmd.extend(["-e", expr])
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    residual = lines[0] if lines else ""
    return proc.returncode, residual, proc.stderr.strip()


def par(parts: list[str]) -> str:
    return "(rho:par " + " ".join(parts) + ")"


def payload(name: str) -> str:
    return f"(rho:send ${name} rho:nil)"


def observations(proc_text: str) -> dict[str, str]:
    proc = parse_mrho_proc(proc_text)
    out: dict[str, str] = {}
    for component in top_level_components(proc):
        if isinstance(component, ProcSend):
            out[contract_name_key(component.channel)] = contract_proc_key(component.payload)
    return out


def public_observations(proc_text: str) -> dict[str, str]:
    return {
        channel: payload
        for channel, payload in observations(proc_text).items()
        if channel.startswith("out")
    }


def components(proc_text: str):
    return top_level_components(parse_mrho_proc(proc_text))


def send_components_on(proc_text: str, channel: str) -> list[ProcSend]:
    out: list[ProcSend] = []
    for component in components(proc_text):
        if isinstance(component, ProcSend) and contract_name_key(component.channel) == channel:
            out.append(component)
    return out


def recv_components_on(proc_text: str, channel: str) -> list[ProcRecv]:
    out: list[ProcRecv] = []
    for component in components(proc_text):
        if isinstance(component, ProcRecv) and contract_name_key(component.channel) == channel:
            out.append(component)
    return out


def expect(ok: bool, name: str, detail: str = "") -> bool:
    if ok:
        print(f"PASS: rhocalc threaded stress {name}")
        return True
    print(f"FAIL: rhocalc threaded stress {name}")
    if detail:
        print(detail)
    return False


def independent_many(bin_path: str) -> bool:
    n = 24
    parts: list[str] = []
    expected: dict[str, str] = {}
    for i in range(n):
        parts.append(f"(rho:recv $c{i} $x (rho:send $out{i} (rho:drop $x)))")
        parts.append(f"(rho:send $c{i} {payload(f'p{i}')})")
        expected[f"out{i}"] = f"send(p{i} nil)"
    status, residual, err = run_rho(bin_path, par(parts))
    return expect(status == 0 and observations(residual) == expected,
                  "independent-many-comms",
                  err or residual)


def hot_channel_linear_consumption(bin_path: str) -> bool:
    sends = 5
    recvs = 3
    parts = [
        f"(rho:recv $hot $x (rho:send $out{i} (rho:drop $x)))"
        for i in range(recvs)
    ]
    parts.extend(f"(rho:send $hot {payload(f'p{i}')})" for i in range(sends))
    status, residual, err = run_rho(bin_path, par(parts))
    obs = public_observations(residual) if residual else {}
    out_channels = {f"out{i}" for i in range(recvs)}
    payloads = list(obs.values())
    hot_sends = send_components_on(residual, "hot") if residual else []
    ok = (
        status == 0 and
        set(obs) == out_channels and
        len(set(payloads)) == recvs and
        all(p.startswith("send(p") and p.endswith(" nil)") for p in payloads) and
        len(hot_sends) == sends - recvs
    )
    return expect(ok, "hot-channel-linear-consumption", err or residual)


def peer_message_occurrences(bin_path: str) -> bool:
    # These are routing names; possessing or spelling one authenticates nobody.
    alice = "(rho:quote (rho:send $pk_alice rho:nil))"
    bob = "(rho:quote (rho:send $pk_bob rho:nil))"
    carol = "(rho:quote (rho:send $pk_carol rho:nil))"
    mallory = "(rho:quote (rho:send $pk_mallory rho:nil))"
    expr = par([
        f"(rho:recv {alice} $x (rho:send $outAlice0 (rho:drop $x)))",
        f"(rho:recv {alice} $x (rho:send $outAlice1 (rho:drop $x)))",
        f"(rho:send {alice} {payload('equal_message')})",
        f"(rho:send {alice} {payload('equal_message')})",
        f"(rho:recv {bob} $x (rho:send $outBob (rho:drop $x)))",
        f"(rho:send {bob} {payload('bob_message')})",
        f"(rho:recv {carol} $x (rho:send $outWrongPeer (rho:drop $x)))",
        f"(rho:send {mallory} {payload('equal_message')})",
    ])
    expected = {
        "outAlice0": "send(equal_message nil)",
        "outAlice1": "send(equal_message nil)",
        "outBob": "send(bob_message nil)",
    }
    for threads in (1, 2, 4):
        status, residual, err = run_rho(bin_path, expr, threads=threads)
        ok = bool(residual) and status == 0 and public_observations(residual) == expected
        if ok:
            sends = [part for part in components(residual) if isinstance(part, ProcSend)]
            recvs = [part for part in components(residual) if isinstance(part, ProcRecv)]
            ok = (
                len(sends) == 4 and len(recvs) == 1 and
                contract_name_key(recvs[0].channel) == "quote(send(pk_carol nil))" and
                sum(contract_name_key(part.channel) == "quote(send(pk_mallory nil))"
                    for part in sends) == 1
            )
        if not ok:
            return expect(False, "peer-message-occurrences", f"threads={threads}: {err or residual}")
    return expect(True, "peer-message-occurrences")


def payload_race(bin_path: str) -> bool:
    expr = par([
        "(rho:recv $hot $x (rho:send $out (rho:drop $x)))",
        f"(rho:send $hot {payload('p0')})",
        f"(rho:send $hot {payload('p1')})",
    ])
    status, residual, err = run_rho(bin_path, expr)
    obs = public_observations(residual) if residual else {}
    hot_sends = send_components_on(residual, "hot") if residual else []
    ok = (
        status == 0 and
        set(obs) == {"out"} and
        obs["out"] in {"send(p0 nil)", "send(p1 nil)"} and
        len(hot_sends) == 1
    )
    return expect(ok, "payload-race", err or residual)


def dequote_spawn(bin_path: str) -> bool:
    expr = par([
        "(rho:recv $a $x (rho:drop $x))",
        f"(rho:send $a (rho:send $b {payload('payload')}))",
        "(rho:recv $b $y (rho:send $out (rho:drop $y)))",
    ])
    status, residual, err = run_rho(bin_path, expr)
    obs = public_observations(residual) if residual else {}
    ok = status == 0 and obs == {"out": "send(payload nil)"}
    return expect(ok, "dequote-spawn", err or residual)


def limit_batch_exact(bin_path: str) -> bool:
    n = 10
    limit = 3
    parts: list[str] = []
    for i in range(n):
        parts.append(f"(rho:recv $c{i} $x (rho:send $out{i} (rho:drop $x)))")
        parts.append(f"(rho:send $c{i} {payload(f'p{i}')})")
    status, residual, err = run_rho(bin_path, par(parts), limit=limit)
    obs = public_observations(residual) if residual else {}
    pending_pairs = 0
    for i in range(n):
        if send_components_on(residual, f"c{i}") and recv_components_on(residual, f"c{i}"):
            pending_pairs += 1
    ok = status == 3 and len(obs) == limit and pending_pairs == n - limit
    return expect(ok, "limit-batch-exact", err or residual)


def alpha_capture_stress(bin_path: str) -> bool:
    n = 6
    parts: list[str] = []
    for i in range(n):
        parts.append(
            f"(rho:recv $c{i} $y "
            f"(rho:recv $d{i} $x (rho:send $out{i} (rho:drop $y))))"
        )
        parts.append(f"(rho:send $c{i} (rho:drop $x))")
        parts.append(f"(rho:send $d{i} rho:nil)")
    status, residual, err = run_rho(bin_path, par(parts))
    obs = public_observations(residual) if residual else {}
    ok = (
        status == 0 and
        set(obs) == {f"out{i}" for i in range(n)} and
        all(value == "drop(x)" for value in obs.values())
    )
    return expect(ok, "alpha-capture-stress", err or residual)


def randomized_threaded_loop(bin_path: str, repeat: int) -> bool:
    rng = random.Random(0xC377A)
    for trial in range(repeat):
        threads = rng.randint(2, 8)
        if trial % 2 == 0:
            n = rng.randint(4, 14)
            parts: list[str] = []
            expected: dict[str, str] = {}
            for i in range(n):
                ch = f"rc{trial}x{i}"
                out = f"outR{trial}x{i}"
                pay = f"pR{trial}x{i}"
                parts.append(f"(rho:recv ${ch} $x (rho:send ${out} (rho:drop $x)))")
                parts.append(f"(rho:send ${ch} {payload(pay)})")
                expected[out] = f"send({pay} nil)"
            parts.append(f"(rho:send $cold{trial} {payload(f'extra{trial}')})")
            parts.append(f"(rho:recv $wait{trial} $x rho:nil)")
            rng.shuffle(parts)
            limit = rng.randint(1, n - 1) if rng.random() < 0.35 else None
            status, residual, err = run_rho(bin_path, par(parts),
                                            threads=threads, limit=limit)
            obs = public_observations(residual) if residual else {}
            if limit is None:
                ok = status == 0 and obs == expected
            else:
                pending_pairs = sum(
                    1
                    for i in range(n)
                    if send_components_on(residual, f"rc{trial}x{i}") and
                       recv_components_on(residual, f"rc{trial}x{i}")
                )
                ok = (
                    status == 3 and
                    len(obs) == limit and
                    pending_pairs == n - limit and
                    all(expected.get(channel) == value
                        for channel, value in obs.items())
                )
        else:
            sends = rng.randint(3, 10)
            recvs = rng.randint(1, sends)
            hot = f"hotR{trial}"
            parts = [
                f"(rho:recv ${hot} $x (rho:send $outHot{trial}x{i} (rho:drop $x)))"
                for i in range(recvs)
            ]
            parts.extend(
                f"(rho:send ${hot} {payload(f'pHot{trial}x{i}')})"
                for i in range(sends)
            )
            rng.shuffle(parts)
            status, residual, err = run_rho(bin_path, par(parts), threads=threads)
            obs = public_observations(residual) if residual else {}
            payload_values = set(obs.values())
            allowed = {f"send(pHot{trial}x{i} nil)" for i in range(sends)}
            ok = (
                status == 0 and
                set(obs) == {f"outHot{trial}x{i}" for i in range(recvs)} and
                len(payload_values) == recvs and
                payload_values <= allowed and
                len(send_components_on(residual, hot)) == sends - recvs
            )
        if not ok:
            return expect(False, "randomized-threaded-loop",
                          err or residual or f"trial {trial}")
    return expect(True, "randomized-threaded-loop")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bin_path")
    parser.add_argument("--repeat", type=int, default=12)
    args = parser.parse_args(argv[1:])
    if args.repeat < 1:
        parser.error("--repeat must be positive")
    bin_path = args.bin_path
    checks = [
        independent_many,
        hot_channel_linear_consumption,
        peer_message_occurrences,
        payload_race,
        dequote_spawn,
        limit_batch_exact,
        alpha_capture_stress,
    ]
    ok = True
    for check in checks:
        ok = check(bin_path) and ok
    ok = randomized_threaded_loop(bin_path, args.repeat) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

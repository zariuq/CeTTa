#!/usr/bin/env python3
"""Run the typed-conversion differential and print one summary.

Kernel queries (`type:eq`, `set:native-proof`) stay in files that contain no
MeTTa equations: an equation in the same `--lang prime` load makes `type:eq`
answer false. The checker is loaded only for its own queries.
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import generate_queries

ROOT = Path("/home/aimama/aihub/hyperon/cetta-prime-2.0-draft-20260910")
CURRENT_BINARY = Path(
    "/shared/zahrada/work/prime-coherence-opus-20260924-claude/CURRENT-BINARY.txt"
)


def comparison_binary() -> Path:
    override = os.environ.get("TC_CETTA")
    if override:
        return Path(override)
    token = CURRENT_BINARY.read_text(encoding="utf-8").split()[0]
    return Path(token)


CETTA = comparison_binary()
CHECKER = ROOT / "lib" / "typed_conversion.metta"
FOCUSED = ROOT / "tests" / "typed_conversion" / "focused.metta"
SPEC_DIR = Path(
    "/home/aimama/aihub/Mettapedia/lean/mettapedia/Mettapedia/TypeTheory/"
    "Calculi/ParameterizedPiSigmaId/TypedEquality"
)
SPEC = SPEC_DIR / "Normalization" / "Algorithmic" / "Relation.lean"
WORK = Path(os.environ.get(
    "TC_WORK", "/shared/zahrada/work/typed-conversion-grok"))
LEDGER = WORK / "ledger.tsv"
SCRATCH = Path(os.environ.get(
    "TC_SCRATCH", "/tmp/claude/grok-goal-6fb6ec8c9416/implementer"))

THEOREM_RE = re.compile(r"set:theorem\s+([A-Za-z_][A-Za-z0-9_@+\-]*)")
AXIOM_RE = re.compile(r"set:axiom\s+&self\s+([A-Za-z_][A-Za-z0-9_@+\-]*)")
PROOF_RE = re.compile(r"set:native-proof\s+([A-Za-z_][A-Za-z0-9_@+\-]*)")
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_@+\-]*$")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def theory_hash() -> str:
    digest = hashlib.sha256()
    files = sorted(p for p in SPEC_DIR.rglob("*.lean") if p.is_file())
    for path in files:
        digest.update(path.relative_to(SPEC_DIR).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def open_reason(row: dict) -> str:
    if row.get("route") == "missing":
        return "export-missing"
    if row["checker"] == "Unresolved" and row["c"] == "Unresolved":
        return "fuel-or-kernel-unreduced"
    if row["checker"] == "Unresolved":
        return "checker-incomplete"
    if row["c"] == "Unresolved":
        return "kernel-unreduced"
    return "open"


# 24 GiB virtual-memory cap. Nested identity β blows up past this; see defects/3.
VM_KB = "25165824"


def run_cetta(src: Path, timeout: int, profile: str | None = None) -> tuple[int, str]:
    if profile:
        script = 'ulimit -v "$1" && exec "$2" --lang prime --profile "$3" "$4"'
        argv = ["run", VM_KB, str(CETTA), profile, str(src)]
    else:
        script = 'ulimit -v "$1" && exec "$2" --lang prime "$3"'
        argv = ["run", VM_KB, str(CETTA), str(src)]
    try:
        proc = subprocess.run(
            ["bash", "-c", script, *argv],
            cwd=str(ROOT),
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        out = (exc.stdout or "") + (exc.stderr or "")
        if isinstance(out, bytes):
            out = out.decode(errors="replace")
        return 124, out
    return proc.returncode, proc.stdout + proc.stderr


def parse_sexprs(text: str):
    i = 0
    n = len(text)
    while i < n:
        if text[i] == ";":
            nl = text.find("\n", i)
            i = n if nl < 0 else nl + 1
            continue
        if text[i] != "(":
            i += 1
            continue
        depth = 0
        j = i
        while j < n:
            if text[j] == ";":
                nl = text.find("\n", j)
                j = n if nl < 0 else nl + 1
                continue
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
                if depth == 0:
                    yield i, j + 1
                    i = j + 1
                    break
            j += 1
        else:
            break


def head_of(sexp: str) -> str:
    body = sexp[1:].lstrip()
    if not body or body[0] == "(":
        return ""
    return body.split()[0].rstrip(")")


def children(sexp: str) -> list[str]:
    inner = sexp[1:-1].strip()
    k = 0
    while k < len(inner) and inner[k] not in " \n\t(":
        k += 1
    return list(_children(inner[k:].strip()))


def _children(rest: str):
    i = 0
    n = len(rest)
    while i < n:
        while i < n and rest[i].isspace():
            i += 1
        if i >= n:
            break
        if rest[i] == ";":
            nl = rest.find("\n", i)
            i = n if nl < 0 else nl + 1
            continue
        if rest[i] == "(":
            depth = 0
            j = i
            while j < n:
                if rest[j] == ";":
                    nl = rest.find("\n", j)
                    j = n if nl < 0 else nl + 1
                    continue
                if rest[j] == "(":
                    depth += 1
                elif rest[j] == ")":
                    depth -= 1
                    if depth == 0:
                        yield rest[i : j + 1]
                        i = j + 1
                        break
                j += 1
            else:
                break
        else:
            j = i
            while j < n and not rest[j].isspace() and rest[j] not in "()":
                j += 1
            yield rest[i:j]
            i = j


def package_names(text: str) -> list[str]:
    found = []
    for rx in (THEOREM_RE, AXIOM_RE, PROOF_RE):
        for name in rx.findall(text):
            if NAME_RE.match(name) and name not in found:
                found.append(name)
    return found


def fixture_files() -> list[Path]:
    out = []
    for base in (ROOT / "tests" / "prime", ROOT / "examples" / "prime"):
        if not base.exists():
            continue
        for path in sorted(base.rglob("*.metta")):
            text = path.read_text(errors="replace")
            if any(token in text for token in (
                "set:native-proof", "set:native-link",
                "set:native-normalize", "set:native-use",
            )):
                out.append(path)
    return out


def phase1_query(name: str, refl: bool, link: bool) -> str:
    """Compare inside the kernel load so printed symbols are not reparsed."""
    blocks = [
        f"""!(let $raw (try (set:native-proof {name}))
   (if (== (car-atom $raw) Established)
     (let $c1 (cdr-atom $raw)
       (let $mid (car-atom $c1)
         (let $d1 (cdr-atom $mid)
           (let $package (car-atom $d1)
             (let (SetNativeProofV1 $n $prop $proof $term $type $context $rules $as $dg) $package
               (let (SetNativeNormalFormV1 $pkg $consumer $app $result $ty $ty2)
                    (set:native-normalize $package (Lam $type (idx 0)))
                 (TERMS phase1 {name} $app $result $ty $rules $context)))))))
     (MISS phase1 {name})))"""
    ]
    if refl:
        blocks.append(
            f"""!(let $raw (try (set:native-proof {name}))
   (if (== (car-atom $raw) Established)
     (let $c1 (cdr-atom $raw)
       (let $mid (car-atom $c1)
         (let $d1 (cdr-atom $mid)
           (let $package (car-atom $d1)
             (let (SetNativeProofV1 $n $prop $proof $term $type $context $rules $as $dg) $package
               (let $nraw (try (set:native-normalize $package (Lam $type (Refl (idx 0)))))
                 (if (== (car-atom $nraw) Established)
                   (let $c2 (cdr-atom $nraw)
                     (let $mid2 (car-atom $c2)
                       (let $d2 (cdr-atom $mid2)
                         (let $norm (car-atom $d2)
                           (if (== (car-atom $norm) SetNativeNormalFormV1)
                             (let (SetNativeNormalFormV1 $pkg $consumer $app $result $ty $ty2) $norm
                               (TERMS phase1-refl {name} $app $result $ty $rules $context))
                             (MISS phase1-refl {name}))))))
                   (MISS phase1-refl {name}))))))))
     (MISS phase1-refl {name})))"""
        )
    if link:
        blocks.append(
            f"""!(let $raw (try (set:native-proof {name}))
   (if (== (car-atom $raw) Established)
     (let $c1 (cdr-atom $raw)
       (let $mid (car-atom $c1)
         (let $d1 (cdr-atom $mid)
           (let $package (car-atom $d1)
             (let $lraw (try (set:native-link identity $package))
               (if (== (car-atom $lraw) Established)
                 (let $c2 (cdr-atom $lraw)
                   (let $mid2 (car-atom $c2)
                     (let $d2 (cdr-atom $mid2)
                       (let $linked (car-atom $d2)
                         (if (== (car-atom $linked) SetNativeLinkedV1)
                           (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
                             (TERMS phase1-link {name} $term $term $type $rules $context))
                           (MISS phase1-link {name}))))))
                 (MISS phase1-link {name})))))))
     (MISS phase1-link {name})))"""
        )
    return "\n".join(blocks)


def write_phase1(path: Path, names: list[str], refl: bool, link: bool) -> Path:
    dest = SCRATCH / "phase1" / (path.stem + ".metta")
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = path.read_text(errors="replace")
    src_dir = path.parent

    def _abs_import(match: re.Match) -> str:
        rel = match.group(1)
        if rel.startswith("./") or rel.startswith("../"):
            return f"!(import! &self {(src_dir / rel).resolve()})"
        return match.group(0)

    text = re.sub(r"!\(import! &self\s+(\S+)\)", _abs_import, text)
    dest.write_text(text)
    with dest.open("a") as fh:
        fh.write("\n")
        for name in names:
            fh.write(phase1_query(name, refl, link))
            fh.write("\n")
    return dest


def scoped_queries() -> list[tuple[str, str, str, str]]:
    rows = []
    base = ROOT / "tests" / "prime"
    if not base.exists():
        return rows
    for path in sorted(base.rglob("*.metta")):
        text = path.read_text(errors="replace")
        if "type:eq" not in text or "PrimeScoped" not in text:
            continue
        for start, end in parse_sexprs(text):
            sexp = text[start:end]
            if head_of(sexp) == "!":
                inner = children(sexp)
                if len(inner) != 1:
                    continue
                sexp = inner[0]
            if head_of(sexp) != "type:eq":
                continue
            kids = children(sexp)
            if len(kids) != 2:
                continue
            if head_of(kids[0]) != "PrimeScoped" or head_of(kids[1]) != "PrimeScoped":
                continue
            lkids = children(kids[0])
            rkids = children(kids[1])
            if len(lkids) != 2 or len(rkids) != 2 or lkids[0] != rkids[0]:
                continue
            rows.append((f"{path.stem}-{len(rows)}", lkids[0], lkids[1], rkids[1]))
    return rows


def write_scoped_c(rows) -> Path:
    dest = SCRATCH / "phase1" / "scoped_c.metta"
    parts = []
    for label, ctx, left, right in rows:
        parts.append(
            f"!(CVERDICT {label} (type:of (PrimeScoped {ctx} {left})) "
            f"(type:eq (PrimeScoped {ctx} {left}) (PrimeScoped {ctx} {right})))"
        )
    dest.write_text("\n".join(parts) + "\n")
    return dest


def adapt_type(type_sexp: str) -> str | None:
    if not type_sexp.startswith("("):
        return None
    head = head_of(type_sexp)
    kids = children(type_sexp)
    if head == "u" and len(kids) == 1 and kids[0].isdigit():
        return f"(Sort (LevelConst {kids[0]}))"
    rename = {
        "id": "Id", "lam": "Lam", "app": "App", "pi": "Pi", "sigma": "Sigma",
        "refl": "Refl", "->": "Pi",
    }
    kernel = {"Sort", "Pi", "Sigma", "Id", "DeclConst", "idx", "App", "Fst", "Snd", "Refl", "Pair", "Lam", "LevelConst"}
    if head in rename or head in kernel:
        adapted = []
        for kid in kids:
            if kid.startswith("("):
                sub = adapt_type(kid)
                if sub is None:
                    return None
                adapted.append(sub)
            else:
                adapted.append(kid)
        return "(" + (rename.get(head, head)) + (" " + " ".join(adapted) if adapted else "") + ")"
    return None


def write_checker(queries: list[tuple]) -> Path:
    """queries: kind, name, left, right, ty, rules, local context, declarations"""
    dest = SCRATCH / "phase1" / "checker.metta"
    lines = [f"!(import! &self {CHECKER})"]
    for kind, name, left, right, ty, rules, local, decls in queries:
        lines.append(
            f"!(KVERDICT {kind} {name} "
            f"(tc:compare 80 none {rules} {local} {decls} {left} {right} {ty}))"
        )
    dest.write_text("\n".join(lines) + "\n")
    return dest


def sanitize_symbols(text: str) -> str:
    """A printed symbol such as all@(Pi num prop) is one atom. Re-parsing
    splits it at the parenthesis. Rewrite @(...) to a single safe symbol
    spelling, identically on every term in the query."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == "@" and i + 1 < n and text[i + 1] == "(":
            depth = 0
            j = i + 1
            while j < n:
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            body = text[i + 2 : j - 1]
            safe = "@" + body.replace(" ", "-").replace("(", "").replace(")", "")
            out.append(safe)
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def sexprs_headed(text: str, head: str) -> list[str]:
    found = []
    for start, end in parse_sexprs(text):
        sexp = text[start:end]
        if head_of(sexp) == head:
            found.append(sexp)
        # cetta wraps a result as [(...)] sometimes inside the text we already
        # have the raw sexp; also unwrap a single-element print wrapper
        if head_of(sexp) == "" or sexp.startswith("[("):
            pass
    # The printer uses square brackets, which our parser skips. Pull the
    # parenthesized payload out of each bracketed line too.
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            inner = line[1:-1].strip()
            if inner.startswith("(") and head_of(inner) == head:
                found.append(inner)
    # dedupe preserving order
    out = []
    for item in found:
        if item not in out:
            out.append(item)
    return out


def a1_scan() -> tuple[bool, str]:
    text = CHECKER.read_text()
    banned = ["type:", "set:", "pf:", "id:eliminate", "import!", "py-atom"]
    hits = [token for token in banned if token in text]
    return (not hits), ("clean" if not hits else "hits " + ",".join(hits))


def focused_ok(text: str) -> tuple[bool, str]:
    want = {
        "beta": "Accepted",
        "delta": "Accepted",
        "delta-short": "Refuted",
        "pi-eta": "Accepted",
        "sigma-eta": "Accepted",
        "drop-pi": "Refuted",
        "drop-sigma": "Refuted",
        "below": "Accepted",
        "shift-fault": "Refuted",
        "endpoint": "Refuted",
        "skip-endpoint": "Accepted",
        "no-cumul": "Refuted",
        "cumul": "Accepted",
        "fuel": "Unresolved",
        "scoped-beta": "Accepted",
        "j-neutral": "Refuted",
        "j-neutral-fault": "Accepted",
        "j-refl-fault": "Refuted",
    }
    found = dict(re.findall(r"\(R\s+(\S+)\s+(\S+)\)", text))
    bad = [k for k, v in want.items() if found.get(k) != v]
    return (not bad), ("ok" if not bad else "mismatch " + ",".join(bad))


def ownership() -> tuple[str, str]:
    proc = subprocess.run(
        ["git", "status", "--short"],
        cwd=str(ROOT),
        text=True,
        capture_output=True,
    )
    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    bad = []
    for ln in lines:
        path = ln[3:].strip().split(" -> ")[-1]
        if path == "lib/typed_conversion.metta" or path.startswith("tests/typed_conversion"):
            continue
        bad.append(ln)
    return "\n".join(lines), "\n".join(bad)


def c_word(token: str) -> str:
    if token == "True":
        return "Accepted"
    if token == "False":
        return "Refuted"
    return "Unresolved"


def run_cetta_bin(binary: Path, src: Path, timeout: int) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            [
                "bash", "-c",
                'ulimit -v "$1" && exec "$2" --lang prime "$3"',
                "run", VM_KB, str(binary), str(src),
            ],
            cwd=str(ROOT),
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        out = (exc.stdout or "") + (exc.stderr or "")
        if isinstance(out, bytes):
            out = out.decode(errors="replace")
        return 124, out
    return proc.returncode, proc.stdout + proc.stderr


def labeled(text: str, head: str) -> dict[str, str]:
    found = {}
    for sexp in sexprs_headed(text, head):
        kids = children(sexp)
        if len(kids) >= 2:
            found[kids[0]] = kids[1]
    return found


def as_checker(token: str) -> str:
    if token in {"Accepted", "Established"}:
        return "Accepted"
    if token == "Refuted" or token.startswith("(Refuted"):
        return "Refuted"
    if token == "Unresolved":
        return "Unresolved"
    return "Unresolved"


def as_c(token: str) -> str:
    if token == "True":
        return "Accepted"
    if token == "False":
        return "Refuted"
    return "Unresolved"


def klass_of(checker: str, sea: str) -> str:
    decided = {"Accepted", "Refuted"}
    if checker in decided and sea in decided:
        return "agree" if checker == sea else "disagree"
    return "open"


def write_gen_batch(rows: list[dict], index: int) -> tuple[Path, Path]:
    folder = SCRATCH / "gen"
    folder.mkdir(parents=True, exist_ok=True)
    c_lines = []
    k_lines = [f"!(import! &self {CHECKER})"]
    for row in rows:
        if row["c_ctx"]:
            c_lines.append(
                f"!(C {row['id']} (type:eq (PrimeScoped {row['c_ctx']} {row['left']}) "
                f"(PrimeScoped {row['c_ctx']} {row['right']})))"
            )
        else:
            c_lines.append(f"!(C {row['id']} (type:eq {row['left']} {row['right']}))")
        k_lines.append(
            f"!(K {row['id']} (tc:compare 40 none {row['rules_tm']} {row['ctx']} Nil "
            f"{row['left']} {row['right']} {row['ty']}))"
        )
    c_path = folder / f"c-{index}.metta"
    k_path = folder / f"k-{index}.metta"
    c_path.write_text("\n".join(c_lines) + "\n")
    k_path.write_text("\n".join(k_lines) + "\n")
    return c_path, k_path


def run_query_rows(rows: list[dict], batch: int, timeout: int) -> list[tuple[dict, str, str]]:
    out = []
    for index, start in enumerate(range(0, len(rows), batch)):
        part = rows[start:start + batch]
        c_path, k_path = write_gen_batch(part, index if batch == 200 else 1000 + index)
        _, cout = run_cetta(c_path, timeout)
        _, kout = run_cetta(k_path, timeout)
        cmap = labeled(cout, "C")
        kmap = labeled(kout, "K")
        for row in part:
            sea = as_c(cmap.get(row["id"], ""))
            chk = as_checker(kmap.get(row["id"], ""))
            out.append((row, chk, sea))
    return out


def run_generated(rows: list[dict]) -> tuple[dict, list[str]]:
    diff = [r for r in rows if r["channel"] == "diff"]
    only = [r for r in rows if r["channel"] == "checker"]
    compared = run_query_rows(diff, 200, 120)
    checked = run_query_rows(only, 20, 60)
    counts = {"agree": 0, "disagree": 0, "open": 0}
    with (WORK / "generator-ledger.tsv").open("w") as fh:
        fh.write("id\tfamily\trules\tchecker\tc\tclass\n")
        for row, chk, sea in compared:
            klass = klass_of(chk, sea)
            counts[klass] += 1
            fh.write(f"{row['id']}\t{row['family']}\t{','.join(row['rules'])}\t{chk}\t{sea}\t{klass}\n")
        for row, chk, _sea in checked:
            fh.write(f"{row['id']}\t{row['family']}\t{','.join(row['rules'])}\t{chk}\tchecker-only\t{chk}\n")
    rule_counts: dict[str, int] = {}
    examples: dict[str, str] = {}
    for row, chk, _sea in compared + checked:
        if row["channel"] == "checker" and chk != row["expect"]:
            continue
        for rule in row["rules"]:
            rule_counts[rule] = rule_counts.get(rule, 0) + 1
            examples.setdefault(rule, row["id"])
    needed = [
        "beta", "delta", "eta", "iota", "partial", "eta-expansion",
        "dependent", "sigma", "id", "j", "recursor", "universe",
    ]
    with (WORK / "rule-table.tsv").open("w") as fh:
        fh.write("rule\tqueries\texample\n")
        for rule in needed:
            fh.write(f"{rule}\t{rule_counts.get(rule, 0)}\t{examples.get(rule, '')}\n")
    missing = [rule for rule in needed if rule_counts.get(rule, 0) < 1]
    decided = counts["agree"] + counts["disagree"]
    total = len(compared)
    lines = [
        f"generator-seed {generate_queries.SEED}",
        f"generator-queries {total}",
        f"generator-max-depth {max(r['depth'] for r in rows)}",
        f"generator-agree {counts['agree']}",
        f"generator-disagree {counts['disagree']}",
        f"generator-open {counts['open']}",
        f"generator-decided {decided}",
        f"generator-decided-fraction {(decided / total) if total else 0:.4f}",
        f"generator-rules {' '.join(needed)}",
        f"generator-rules-missing {' '.join(missing) if missing else 'none'}",
    ]
    only_bad = [row["id"] for row, chk, _sea in checked if chk != row["expect"]]
    lines.append(f"generator-checker-only {' '.join(only_bad) if only_bad else 'ok'}")
    return {
        "disagree": counts["disagree"],
        "missing": missing,
        "only_bad": only_bad,
        "fraction": (decided / total) if total else 0,
    }, lines


def run_stability() -> tuple[bool, list[str]]:
    equations = generate_queries.stability_equations()
    items = []
    for eq in equations:
        for sub in eq["subs"]:
            items.append((eq, sub))
    rows = []
    if items:
        fake = []
        for eq, sub in items:
            fake.append({
                "id": f"stab-{eq['id']}-{sub['n']}",
                "left": sub["left"],
                "right": sub["right"],
                "ty": eq["ty"],
                "ctx": "Nil",
                "c_ctx": None,
                "rules_tm": "LNil",
            })
        compared = run_query_rows(fake, 40, 600)
        by_id = {row["id"]: (chk, sea) for row, chk, sea in compared}
    else:
        by_id = {}
    refuted = 0
    tested = 0
    with (WORK / "stability.tsv").open("w") as fh:
        fh.write("equation\tn\tchecker\tc\tnote\n")
        for eq in equations:
            if not eq["subs"]:
                fh.write(f"{eq['id']}\t\t\t\t{eq['note']}\n")
                continue
            for sub in eq["subs"]:
                sid = f"stab-{eq['id']}-{sub['n']}"
                chk, sea = by_id.get(sid, ("Unresolved", "Unresolved"))
                tested += 1
                if chk == "Refuted" or sea == "Refuted":
                    refuted += 1
                fh.write(f"{eq['id']}\t{sub['n']}\t{chk}\t{sea}\t\n")
    ok = refuted == 0 and tested >= 40
    named = []
    for eq in equations:
        if eq["subs"]:
            named.append(f"{eq['id']}={len(eq['subs'])}")
        else:
            named.append(f"{eq['id']}=uninhabited")
    lines = [
        f"stability-equations {len(equations)}",
        f"stability-families {' '.join(named)}",
        "stability-uninhabited phase1-sort0-variable",
        f"stability-substitutions {tested}",
        f"stability-refuted {refuted}",
        f"stability {'green' if ok else 'red'}",
    ]
    return ok, lines


FAULTS = (
    ("drop-pi-eta", "pi-eta", "drop-pi"),
    ("drop-sigma-eta", "sigma-eta", "drop-sigma"),
    ("delta-below-arity", "delta-short", "below"),
    ("fire-j-neutral", "j-neutral", "j-neutral-fault"),
    ("skip-j-refl", "iota", "j-refl-fault"),
    ("wrong-shift", "beta-shift", "shift-fault"),
    ("no-cumulativity", "cumul", "no-cumul"),
)


def run_faults(focused_out: str) -> tuple[bool, list[str]]:
    found = dict(re.findall(r"\(R\s+(\S+)\s+(\S+)\)", focused_out))
    red = 0
    lines = []
    with (WORK / "faults.tsv").open("w") as fh:
        fh.write("fault\tgood\tfaulty\tred\n")
        for name, good_key, bad_key in FAULTS:
            good = found.get(good_key, "")
            bad = found.get(bad_key, "")
            decided = {good, bad} <= {"Accepted", "Refuted"} and good != bad
            if decided:
                red += 1
            fh.write(f"{name}\t{good_key}={good}\t{bad_key}={bad}\t{int(bool(decided))}\n")
            lines.append(f"fault {name} {good_key}={good} {bad_key}={bad} {'red' if decided else 'green'}")
    return red == len(FAULTS), lines


def run_kernel_mutation() -> tuple[bool, list[str]]:
    script = ROOT / "scripts" / "mutate_prime_regular_kernel_engine_failure.py"
    source = ROOT / "src" / "prime_regular_kernel.c"
    dest = WORK / "prime_regular_kernel.engine-failure.c"
    subprocess.check_call(["python3", str(script), str(source), str(dest)])
    mutated = dest.read_text(encoding="utf-8")
    original = source.read_text(encoding="utf-8")
    patched = (
        mutated.count("if (true)") >= 2
        and "if (!ok || !substituted)" not in mutated
        and "if (!ok || !reduct)" not in mutated
        and original.count("if (!ok || !substituted)") == 1
        and original.count("if (!ok || !reduct)") == 1
    )
    binary = WORK / "cetta-engine-failure"
    if not binary.exists():
        binary = Path("/shared/zahrada/work/typed-conversion-grok/cetta-engine-failure")
    probe = SCRATCH / "kernel-beta.metta"
    probe.write_text(
        "!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0)) "
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0)))\n"
        "!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0)) "
        "(PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) "
        "(App (Lam (Sort (LevelConst 0)) (idx 0)) (idx 0))))\n"
    )
    _, good = run_cetta(probe, 30)
    _, bad = run_cetta_bin(binary, probe, 30) if binary.exists() else (127, "")
    good_lines = [ln.strip() for ln in good.splitlines() if ln.strip()]
    bad_lines = [ln.strip() for ln in bad.splitlines() if ln.strip()]
    red = (
        patched
        and binary.exists()
        and good_lines[:2] == ["[True]", "[True]"]
        and len(bad_lines) >= 2
        and bad_lines[0] == "[True]"
        and bad_lines[1] != "[True]"
    )
    lines = [
        f"kernel-mutation-script {'patched' if patched else 'not-patched'}",
        f"kernel-mutation-good {' '.join(good_lines[:2])}",
        f"kernel-mutation-fault {' '.join(bad_lines[:2])}",
        f"kernel-mutation {'red' if red else 'green'}",
    ]
    return red, lines


PROFILES = (
    "identity-j",
    "identity-scoped",
    "identity-uip",
    "identity-univalence",
)


def abs_imports(text: str, src_dir: Path) -> str:
    def _abs_import(match: re.Match) -> str:
        rel = match.group(1)
        if rel.startswith("./") or rel.startswith("../"):
            return f"!(import! &self {(src_dir / rel).resolve()})"
        return match.group(0)

    return re.sub(r"!\(import! &self\s+(\S+)\)", _abs_import, text)


def top_judgments(text: str) -> list[tuple[int, int, str]]:
    """Top-level !(type:eq ...) and !(type:check ...) spans, from the bang."""
    found = []
    for start, end in parse_sexprs(text):
        sexp = text[start:end]
        if head_of(sexp) not in ("type:eq", "type:check"):
            continue
        j = start - 1
        while j >= 0 and text[j] in " \t":
            j -= 1
        if j < 0 or text[j] != "!":
            continue
        if "$" in sexp:
            continue
        found.append((j, end, sexp))
    return found


def phase2_sources() -> list[Path]:
    out = []
    for base in (ROOT / "tests" / "prime", ROOT / "examples" / "prime"):
        if not base.exists():
            continue
        for path in sorted(base.rglob("*.metta")):
            text = path.read_text(errors="replace")
            if "type:eq" in text or "type:check" in text:
                out.append(path)
    return out


def split_context(ctx: str) -> tuple[str, str]:
    variables = []
    decls = []
    cur = ctx
    while cur and cur not in ("PrimeCtxNil", "Nil"):
        head = head_of(cur)
        kids = children(cur)
        if head == "PrimeCtxCons" and len(kids) == 2:
            variables.append(kids[0])
            cur = kids[1]
        elif head == "PrimeCtxDecl" and len(kids) == 3:
            decls.append((kids[0], kids[1]))
            cur = kids[2]
        else:
            break
    var_ctx = "Nil"
    for ty in reversed(variables):
        var_ctx = f"(Cons {ty} {var_ctx})"
    decl_ctx = "Nil"
    for const, ty in reversed(decls):
        decl_ctx = f"(PrimeCtxDecl {const} {ty} {decl_ctx})"
    return var_ctx, decl_ctx


def field_args(sexp: str) -> dict[str, list[str]]:
    out = {}
    for kid in children(sexp):
        out[head_of(kid)] = children(kid)
    return out


def one_field(fields: dict[str, list[str]], name: str) -> str | None:
    args = fields.get(name)
    if not args:
        return None
    return args[0]


def unsynthesized(term: str | None) -> bool:
    return term is None or head_of(term) == "Unsynthesized"


def adapt_universes(term: str) -> str:
    """Closed elaboration prints universe i as the atom Ui."""
    def walk(node):
        if isinstance(node, str):
            if len(node) > 1 and node[0] == "U" and node[1:].isdigit():
                return ["Sort", ["LevelConst", node[1:]]]
            return node
        return [walk(x) if not isinstance(x, str) else (
            ["Sort", ["LevelConst", x[1:]]] if len(x) > 1 and x[0] == "U" and x[1:].isdigit() else x
        ) for x in node]

    if not term or term[0] != "(":
        if len(term) > 1 and term[0] == "U" and term[1:].isdigit():
            return f"(Sort (LevelConst {term[1:]}))"
        return term
    node, _ = generate_queries.parse(term)
    return generate_queries.emit(walk(node))


def run_phase2() -> tuple[dict, list[str]]:
    folder = SCRATCH / "phase2"
    folder.mkdir(parents=True, exist_ok=True)
    jobs = []
    rows = []
    sources = phase2_sources()
    for path in sources:
        profiles = PROFILES if path.name == "identity_matrix.metta" else (None,)
        text = abs_imports(path.read_text(errors="replace"), path.parent)
        spans = top_judgments(text)
        if not spans:
            continue
        pieces = []
        last = 0
        local = []
        fuels = {}
        for n, (start, end, inner) in enumerate(spans):
            qid = f"q{n}"
            kids = children(inner)
            if kids and kids[-1].isdigit():
                fuels[qid] = int(kids[-1])
            pieces.append(text[last:start])
            pieces.append(
                f"!(C2 {qid} {inner})\n!(K2 {qid} (type:kernel-query {inner}))"
            )
            last = end
            local.append(qid)
        pieces.append(text[last:])
        driver = "".join(pieces)
        for profile in profiles:
            tag = profile or "default"
            dest = folder / f"{path.stem}-{tag}.metta"
            dest.write_text(driver)
            _rc, out = run_cetta(dest, 180, profile)
            (folder / f"{path.stem}-{tag}.out").write_text(out)
            c_map = labeled(out, "C2")
            k_map = labeled(out, "K2")
            rel = str(path.relative_to(ROOT))
            for qid in local:
                sea = as_c(c_map.get(qid, ""))
                exported = k_map.get(qid, "")
                if head_of(exported) != "PrimeKernelQueryV1":
                    rows.append({
                        "source": rel, "profile": tag, "id": qid,
                        "kind": "", "route": "missing",
                        "checker": "Unresolved", "c": sea,
                        "fragment": False, "class": "open",
                    })
                    continue
                fields = field_args(exported)
                route = one_field(fields, "Route") or "missing"
                judgment = one_field(fields, "Judgment") or ""
                kind = head_of(judgment) if judgment else ""
                fuel = fuels.get(qid, 256)
                if route == "outside":
                    rows.append({
                        "source": rel, "profile": tag, "id": qid,
                        "kind": kind, "route": route,
                        "checker": "Unresolved", "c": sea,
                        "fragment": False, "class": "candidate-only",
                    })
                    continue
                subject = one_field(fields, "Subject")
                subject_ty = one_field(fields, "SubjectType")
                obj = one_field(fields, "Object")
                obj_ty = one_field(fields, "ObjectType")
                level_blob = " ".join(
                    x or "" for x in (subject, subject_ty, obj, obj_ty, one_field(fields, "Context"))
                )
                if "LevelParam" in level_blob:
                    rows.append({
                        "source": rel, "profile": tag, "id": qid,
                        "kind": kind, "route": route,
                        "checker": "Unresolved", "c": sea,
                        "fragment": False, "class": "candidate-only",
                    })
                    continue
                rules = adapt_universes(one_field(fields, "Rules") or "LNil")
                ctx_tm = adapt_universes(one_field(fields, "Context") or "Nil")
                subject = adapt_universes(subject) if subject else subject
                subject_ty = adapt_universes(subject_ty) if subject_ty else subject_ty
                obj = adapt_universes(obj) if obj else obj
                obj_ty = adapt_universes(obj_ty) if obj_ty else obj_ty
                var_ctx, decl_ctx = split_context(ctx_tm)
                if kind == "type:check":
                    intro = head_of(subject or "") in {"Pair", "Lam", "Refl"}
                    synth_ok = subject_ty is not None and not unsynthesized(subject_ty)
                    comparable = (
                        subject is not None and obj is not None
                        and not unsynthesized(obj)
                        and (synth_ok or intro)
                        and obj_ty is not None and not unsynthesized(obj_ty)
                    )
                    job_call = {
                        "mode": "check",
                        "term": subject, "expected": obj,
                        "synth": subject_ty if synth_ok else "Miss",
                        "at": obj_ty,
                    }
                else:
                    comparable = (
                        subject is not None and obj is not None
                        and not unsynthesized(subject_ty) and not unsynthesized(obj_ty)
                    )
                    job_call = {
                        "mode": "eq",
                        "left": subject, "right": obj, "ty": subject_ty,
                    }
                if not comparable:
                    rows.append({
                        "source": rel, "profile": tag, "id": qid,
                        "kind": kind, "route": route,
                        "checker": "Unresolved", "c": sea,
                        "fragment": False, "class": "candidate-only",
                    })
                    continue
                jid = f"{path.stem}-{tag}-{qid}"
                jobs.append({
                    "id": jid, "row": len(rows),
                    "rules": rules, "ctx": var_ctx, "decls": decl_ctx,
                    "fuel": fuel,
                    **job_call,
                })
                rows.append({
                    "source": rel, "profile": tag, "id": qid,
                    "kind": kind, "route": route,
                    "checker": "Unresolved", "c": sea,
                    "fragment": True, "class": "open",
                })
    for start in range(0, len(jobs), 40):
        part = jobs[start:start + 40]
        lines = [f"!(import! &self {CHECKER})"]
        for job in part:
            if job["mode"] == "check":
                call = (
                    f"(tc:check {job['fuel']} none {job['rules']} {job['ctx']} {job['decls']} "
                    f"{job['term']} {job['expected']} {job['synth']} {job['at']})"
                )
            else:
                call = (
                    f"(tc:compare {job['fuel']} none {job['rules']} {job['ctx']} {job['decls']} "
                    f"{job['left']} {job['right']} {job['ty']})"
                )
            lines.append(f"!(K {job['id']} {call})")
        dest = folder / f"checker-{start}.metta"
        dest.write_text("\n".join(lines) + "\n")
        _rc, out = run_cetta(dest, 180)
        found = labeled(out, "K")
        for job in part:
            rows[job["row"]]["checker"] = as_checker(found.get(job["id"], ""))
    counts = {"agree": 0, "disagree": 0, "open": 0, "candidate-only": 0}
    fragment = 0
    open_lines = []
    with (WORK / "phase2-ledger.tsv").open("w") as fh:
        fh.write("source\tprofile\tid\tkind\troute\tchecker\tc\tfragment\tclass\treason\n")
        for row in rows:
            if row["fragment"]:
                row["class"] = klass_of(row["checker"], row["c"])
                fragment += 1
            counts[row["class"]] = counts.get(row["class"], 0) + 1
            reason = open_reason(row) if row["class"] == "open" else ""
            if row["class"] == "open":
                open_lines.append(
                    f"unresolved {row['source']} {row['profile']} {row['id']} "
                    f"{row['kind']} {reason}"
                )
            fh.write(
                f"{row['source']}\t{row['profile']}\t{row['id']}\t{row['kind']}\t"
                f"{row['route']}\t{row['checker']}\t{row['c']}\t"
                f"{int(row['fragment'])}\t{row['class']}\t{reason}\n"
            )
    (WORK / "unresolved.tsv").write_text(
        "source\tprofile\tid\tkind\treason\n" + "".join(
            line.replace("unresolved ", "").replace(" ", "\t", 4) + "\n"
            for line in open_lines
        )
    )
    decided = counts["agree"] + counts["disagree"]
    lines = [
        f"phase2-files {len(sources)}",
        f"phase2-queries {len(rows)}",
        f"phase2-candidate-only {counts['candidate-only']}",
        f"phase2-fragment {fragment}",
        f"phase2-agree {counts['agree']}",
        f"phase2-disagree {counts['disagree']}",
        f"phase2-open {counts['open']}",
        f"phase2-decided {decided}",
        f"phase2-decided-fraction {(decided / fragment) if fragment else 0:.4f}",
        *open_lines,
    ]
    return {
        "disagree": counts["disagree"],
        "fraction": (decided / fragment) if fragment else 0.0,
        "queries": len(rows),
    }, lines


def run_budgets() -> tuple[bool, list[str]]:
    fam = (
        "(PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0))) "
        "(PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))"
    )
    ctx = (
        "(Cons (Pi (idx 0) (Sort (LevelConst 0))) "
        "(Cons (Sort (LevelConst 0)) Nil))"
    )
    families = (
        ("SubtypeCodomain", "(Pi (idx 1) (Sort (LevelConst 1)))"),
        ("SubtypeDomain", "(Pi (Sort (LevelConst 1)) (Sort (LevelConst 0)))"),
    )
    c_lines = []
    k_lines = [f"!(import! &self {CHECKER})"]
    labels = []
    for name, expected in families:
        for budget in (1, 8, 64, 256):
            label = f"{name}-{budget}"
            labels.append((name, budget, label))
            c_lines.append(
                f"!(B {label} (type:check (PrimeScoped {fam} (idx 0)) {expected} {budget}))"
            )
            k_lines.append(
                f"!(B {label} (tc:check {budget} none LNil {ctx} Nil (idx 0) "
                f"{expected} Miss Open))"
            )
    folder = SCRATCH / "budget"
    folder.mkdir(parents=True, exist_ok=True)
    c_path = folder / "kernel.metta"
    k_path = folder / "checker.metta"
    c_path.write_text("\n".join(c_lines) + "\n")
    k_path.write_text("\n".join(k_lines) + "\n")
    _, cout = run_cetta(c_path, 120)
    _, kout = run_cetta(k_path, 120)
    cmap = labeled(cout, "B")
    kmap = labeled(kout, "B")
    lines = []
    ok = True
    seen = {}
    for name, budget, label in labels:
        sea = as_c(cmap.get(label, ""))
        chk = as_checker(kmap.get(label, ""))
        if chk != sea:
            ok = False
        prev = seen.get(name)
        if prev and prev in {"Accepted", "Refuted"} and chk in {"Accepted", "Refuted"} and prev != chk:
            ok = False
            lines.append(f"budget-flip {name} {prev} {chk}")
        if chk in {"Accepted", "Refuted"}:
            seen[name] = chk
        lines.append(f"budget {name} {budget} checker={chk} kernel={sea}")
    lines.append(f"budget {'green' if ok else 'red'}")
    (WORK / "budget.tsv").write_text("\n".join(lines) + "\n")
    return ok, lines


def main() -> int:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    (SCRATCH / "phase1").mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    a1_pass, a1_note = a1_scan()
    code, focused_out = run_cetta(FOCUSED, 60)
    (SCRATCH / "focused.out").write_text(focused_out)
    fok, fnote = focused_ok(focused_out)

    extracted = []  # scoped queries still go through a second checker process
    files = fixture_files()
    for path in files:
        text = path.read_text(errors="replace")
        names = package_names(text)
        refl = "(Refl (idx 0))" in text
        link = "set:native-link identity" in text
        driver = write_phase1(path, names, refl, link)
        _rc, out = run_cetta(driver, 120)
        (SCRATCH / "phase1" / (path.stem + ".out")).write_text(out)
        for sexp in sexprs_headed(sanitize_symbols(out), "TERMS"):
            kids = children(sexp)
            if len(kids) != 7:
                continue
            kind, name, left, right, ty, rules, ctx = kids
            extracted.append((kind, name, left, right, ty, rules, "Nil", ctx, "Accepted", str(path.relative_to(ROOT))))

    scoped = scoped_queries()
    scoped_c = {}
    scoped_ty = {}
    if scoped:
        driver = write_scoped_c(scoped)
        _rc, out = run_cetta(driver, 60)
        (SCRATCH / "phase1" / "scoped_c.out").write_text(out)
        for sexp in sexprs_headed(out, "CVERDICT"):
            kids = children(sexp)
            if len(kids) != 3:
                continue
            label, ty, verdict = kids
            scoped_c[label] = c_word(verdict)
            scoped_ty[label] = ty
        for label, ctx, left, right in scoped:
            ty = adapt_type(scoped_ty.get(label, ""))
            sea = scoped_c.get(label, "Unresolved")
            if ty is None:
                extracted.append(("scoped", label, left, right, "Nil", "LNil", ctx, "Nil", sea, "tests/prime PrimeScoped"))
                continue
            extracted.append(("scoped", label, left, right, ty, "LNil", ctx, "Nil", sea, "tests/prime PrimeScoped"))

    checker_inputs = [
        (kind, name, left, right, ty, rules, local, decls)
        for (kind, name, left, right, ty, rules, local, decls, _sea, _src) in extracted
        if ty != "Nil"
    ]
    checker_verdicts = {}
    checker_log = []
    for item in checker_inputs:
        driver = write_checker([item])
        _rc, out = run_cetta(driver, 30)
        checker_log.append(out)
        got = False
        for sexp in sexprs_headed(out, "KVERDICT"):
            kids = children(sexp)
            if len(kids) == 3 and kids[2] in {"Accepted", "Refuted", "Unresolved"}:
                checker_verdicts[(kids[0], kids[1])] = kids[2]
                got = True
        if not got:
            checker_verdicts[(item[0], item[1])] = "Unresolved"
    (SCRATCH / "phase1" / "checker.out").write_text("\n".join(checker_log))

    rows = []
    for kind, name, _l, _r, ty, _rules, _local, _decls, sea, source in extracted:
        if ty == "Nil":
            checker = "Unresolved"
        else:
            checker = checker_verdicts.get((kind, name), "Unresolved")
        rows.append((source, kind, name, checker, sea))

    counts = {"agree": 0, "disagree": 0, "open": 0}
    decided_set = {"Accepted", "Refuted"}
    with LEDGER.open("w") as fh:
        fh.write("source\tkind\tname\tchecker\tc\tclass\n")
        for source, kind, name, checker, sea in rows:
            if checker in decided_set and sea in decided_set:
                klass = "agree" if checker == sea else "disagree"
            else:
                klass = "open"
            counts[klass] += 1
            fh.write(f"{source}\t{kind}\t{name}\t{checker}\t{sea}\t{klass}\n")
    decided = counts["agree"] + counts["disagree"]
    total = len(rows)
    status, bad = ownership()
    (SCRATCH / "ownership.txt").write_text(status + ("\n\nOutside ownership:\n" + bad if bad else "\n"))
    gen_rows = generate_queries.build()
    gen_status, gen_lines = run_generated(gen_rows)
    stab_ok, stab_lines = run_stability()
    faults_ok, fault_lines = run_faults(focused_out)
    kernel_ok, kernel_lines = run_kernel_mutation()
    phase2_status, phase2_lines = run_phase2()
    budget_ok, budget_lines = run_budgets()
    summary = "\n".join([
        f"binary {sha256(CETTA)}",
        f"specification {theory_hash()}",
        f"specification-relation {sha256(SPEC)}",
        f"checker {sha256(CHECKER)}",
        f"A1 {a1_note}",
        f"focused {fnote} exit {code}",
        f"fixtures {len(files)}",
        f"scoped-queries {len(scoped)}",
        f"rows {total}",
        f"agree {counts['agree']}",
        f"disagree {counts['disagree']}",
        f"open {counts['open']}",
        f"decided {decided}",
        f"decided-fraction {(decided / total) if total else 0:.4f}",
        f"agreement-among-decided {(counts['agree'] / decided) if decided else 0:.4f}",
        f"ownership-outside {len(bad.splitlines()) if bad else 0}",
        *phase2_lines,
        *budget_lines,
        *gen_lines,
        *stab_lines,
        *fault_lines,
        *kernel_lines,
        "completion " + ("yes" if (
            a1_pass and fok and counts["disagree"] == 0
            and gen_status["disagree"] == 0
            and not gen_status["missing"]
            and not gen_status["only_bad"]
            and gen_status["fraction"] >= 0.95
            and stab_ok and faults_ok and kernel_ok
            and phase2_status["disagree"] == 0
            and phase2_status["queries"] > 0
            and phase2_status["fraction"] >= 0.95
            and budget_ok
        ) else "no"),
    ])
    print(summary)
    (WORK / "summary.txt").write_text(summary + "\n")
    gates = (
        a1_pass and fok and counts["disagree"] == 0
        and gen_status["disagree"] == 0
        and not gen_status["missing"]
        and not gen_status["only_bad"]
        and gen_status["fraction"] >= 0.95
        and stab_ok and faults_ok and kernel_ok
        and phase2_status["disagree"] == 0
        and phase2_status["queries"] > 0
        and phase2_status["fraction"] >= 0.95
        and budget_ok
    )
    return 0 if gates else 1


if __name__ == "__main__":
    sys.exit(main())

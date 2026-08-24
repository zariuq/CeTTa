#!/usr/bin/env python3
"""Package one authored IGGP source and type profile without interpreting it."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_iggp_manifest as corpus  # noqa: E402
from check_prime_iggp_presentations import (  # noqa: E402
    audit as audit_presentations,
    source_path,
    validate as validate_presentations,
)
from prime_iggp_generation import GenerationError, materialize_outputs  # noqa: E402


DEFAULT_GAME = "scissors_paper_stone"
REVISION_DOMAIN = b"cetta.gdl-type-source.v1\0"


def source_revision(source: bytes, profile: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(REVISION_DOMAIN)
    digest.update(len(source).to_bytes(8, "big"))
    digest.update(source)
    digest.update(len(profile).to_bytes(8, "big"))
    digest.update(profile)
    return "gdl-type-source-" + digest.hexdigest()


def render_source_package(source: bytes, profile: bytes) -> str:
    if b"\0" in source or b"\0" in profile:
        raise GenerationError("GDL source/profile contains a NUL byte")
    try:
        source_text = source.decode("utf-8")
        profile_text = profile.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise GenerationError("GDL source/profile is not UTF-8") from exc
    source_digest = hashlib.sha256(source).hexdigest()
    profile_digest = hashlib.sha256(profile).hexdigest()
    revision = source_revision(source, profile)
    quoted_source = json.dumps(source_text, ensure_ascii=True)
    quoted_profile = json.dumps(profile_text, ensure_ascii=True)
    return "\n".join(
        (
            "(gdl-type-source-v1",
            f'  (source-digest "{source_digest}")',
            f'  (profile-digest "{profile_digest}")',
            f"  (revision {revision})",
            f"  (source-text {quoted_source})",
            f"  (profile-text {quoted_profile}))",
            "",
        )
    )


def source_package(
    snapshot_root: Path, game: str
) -> tuple[str, str, str, str]:
    games, digest = audit_presentations(snapshot_root)
    validate_presentations(games, digest)
    if not any(item.game == game for item in games):
        raise GenerationError(f"unknown IGGP game {game!r}")
    source = source_path(snapshot_root, game).read_bytes()
    profile = (snapshot_root / "types" / f"{game}.typ").read_bytes()
    return (
        render_source_package(source, profile),
        hashlib.sha256(source).hexdigest(),
        hashlib.sha256(profile).hexdigest(),
        source_revision(source, profile),
    )


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--game", choices=corpus.GAMES, default=DEFAULT_GAME)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        rendered, source_digest, profile_digest, revision = source_package(
            args.snapshot_root, args.game
        )
        output = args.output or (
            repo / "lib/ilp" / f"iggp_{args.game}_type_source.generated.metta"
        )
        materialize_outputs(((output, rendered),), args.check)
    except (GenerationError, OSError) as exc:
        print(f"FAIL: IGGP {args.game} type source: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} IGGP {args.game} "
        f"authority-free type source {source_digest[:12]}/"
        f"{profile_digest[:12]} at {revision}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

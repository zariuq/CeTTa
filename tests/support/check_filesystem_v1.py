#!/usr/bin/env python3
"""Owned filesystem observations across the core-C language hosts."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/filesystem-v1-tests")
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    empty, nested = output / "empty", output / "nested"
    empty.mkdir(exist_ok=True)
    nested.mkdir(exist_ok=True)
    data = output / "bytes"
    data.write_bytes(b"a\x00\xffz")
    large_data = output / "large-bytes"
    large_data.write_bytes(b"a\x00\xffz" * 16384)
    (nested / "first").write_bytes(b"first")
    (nested / "second").mkdir(exist_ok=True)
    link = output / "link"
    if not link.is_symlink():
        link.symlink_to("bytes")
    walk = output / "walk-exclusions"
    (walk / "keep").mkdir(parents=True, exist_ok=True)
    (walk / "keep/leaf").write_bytes(b"ordinary")
    (walk / "skip").mkdir(exist_ok=True)
    (walk / "skip/hidden").write_bytes(b"ordinary")
    target, links, cycle = output / "walk-target", output / "walk-links", output / "walk-cycle"
    for directory in (target, links, cycle): directory.mkdir(exist_ok=True)
    (target / "leaf").write_bytes(b"ordinary")
    if not (links / "alias").is_symlink(): (links / "alias").symlink_to("../walk-target")
    if not (cycle / "loop").is_symlink(): (cycle / "loop").symlink_to(".")
    aliases = output / "walk-aliases"
    aliases.mkdir(exist_ok=True)
    for name in ("first", "second"):
        if not (aliases / name).is_symlink(): (aliases / name).symlink_to("../walk-target")
    count = 0

    def run(language, name, source=None, path=None, *, cwd=ROOT, flags=(), inline=False):
        nonlocal count
        if source is None:
            script = ROOT / "tests/filesystem_v1" / f"{name}.metta"
        else:
            script = output / f"{language}-{name}.metta"
            script.write_text(source, encoding="utf-8")
        command = [str(binary), "--lang", language]
        if language != "prime":
            command += ["--profile", "extended"]
        program = ["-e", script.read_text(encoding="utf-8")] if inline else [str(script)]
        result = subprocess.run(command + list(flags) + program +
                                ([str(path)] if path is not None else []),
                                cwd=cwd, capture_output=True, timeout=30)
        (output / f"{language}-{name}.stdout").write_bytes(result.stdout)
        (output / f"{language}-{name}.stderr").write_bytes(result.stderr)
        count += 1
        assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
        assert b"forbidden" not in result.stdout, (name, result.stdout)
        return result.stdout

    prefix = "!(import! &self fs)\n!(import! &self system)\n"
    for language in args.language or ("he", "petta", "prime"):
        staged, published = output / f"{language}-staged", output / f"{language}-published"
        staged.write_bytes(b"new\x00content")
        staged.chmod(0o640)
        published.write_bytes(b"old content")
        observed = run(language, "rename-replace", prefix +
            f'!(fs:rename {json.dumps(str(staged))} {json.dumps(str(published))})\n')
        assert b"fs:failed" not in observed and published.read_bytes() == b"new\x00content", observed
        assert not staged.exists() and published.stat().st_mode & 0o777 == 0o640
        observed = run(language, "rename-missing", prefix +
            f'!(fs:rename {json.dumps(str(staged))} {json.dumps(str(published))})\n')
        assert b"fs:failed fs:missing rename" in observed and published.read_bytes() == b"new\x00content", observed
        observed = run(language, "protocol_empty_data_positive", cwd=empty)
        assert b"(FsDelivered (EmptyBytes))" in observed and b"(FsComplete)" not in observed, observed
        observed = run(language, "protocol_end_positive", cwd=empty)
        assert b"(FsComplete)" in observed and b"(FsObserve (FsEnded))" in observed, observed
        observed = run(language, "protocol_unfinished_negative", cwd=empty)
        assert b"(FsPending)" in observed and b"(FsFailed (PermissionFault))" in observed, observed
        assert b"(FsUnfinished (Cancelled))" in observed and b"(FsComplete)" not in observed, observed
        assert b"LangDef:TermRejected" in observed, observed
        observed = run(language, "directory_empty_positive", path=empty)
        assert b"(DirectoryEmpty (fs:end 0) fs:closed)" in observed, observed
        observed = run(language, "walk_exclusions_positive", path=walk)
        assert observed.count(b"fs:walk-entry") == 2 and b'"keep/leaf"' in observed, observed
        assert b'"skip"' not in observed and b"(fs:end 3) fs:closed" in observed, observed
        observed = run(language, "walk_links_positive", path=links)
        assert observed.count(b"fs:walk-entry") == 2 and b'"alias/leaf"' in observed, observed
        assert b"(fs:end 2) fs:closed" in observed, observed
        observed = run(language, "walk_closed_negative", path=walk)
        assert b"fs:failed fs:invalid-handle" in observed and b"fs:end" not in observed, observed
        source = prefix + f'!(let $owner (fs:walk-open {json.dumps(str(links))}) ' \
            + '(let $one (fs:walk-next $owner no-follow ()) ' \
            + '(let $end (fs:walk-next $owner no-follow ()) ' \
            + '(let $closed (fs:close $owner) (NoDirectoryLinks $one $end $closed)))))\n'
        observed = run(language, "walk-no-follow", source)
        assert observed.count(b"fs:walk-entry") == 1 and b"(fs:end 1) fs:closed" in observed, observed
        source = prefix + f'!(let $owner (fs:walk-open {json.dumps(str(cycle))}) ' \
            + '(let $one (fs:walk-next $owner follow ()) ' \
            + '(let $fault (fs:walk-next $owner follow ()) ' \
            + '(let $closed (fs:close $owner) (Cycle $one $fault $closed)))))\n'
        observed = run(language, "walk-ancestor-cycle", source)
        assert b"fs:failed fs:cycle walk-step" in observed and b"fs:end" not in observed, observed
        if language != "prime":
            observed = run(language, "walk_interrupted_negative", path=links)
            assert b"fs:walk-ready fs:closed" in observed and b"fs:end" not in observed, observed
            assert b"fs:walk-entry" not in observed, observed
            source = prefix + f'!(let $owner (fs:walk-open {json.dumps(str(aliases))}) ' \
                + '(let $a (fs:walk-next $owner follow ()) (let $b (fs:walk-next $owner follow ()) ' \
                + '(let $c (fs:walk-next $owner follow ()) (let $d (fs:walk-next $owner follow ()) ' \
                + '(let $end (fs:walk-next $owner follow ()) ' \
                + '(let $closed (fs:close $owner) (DistinctAliases $a $b $c $d $end $closed))))))))\n'
            observed = run(language, "walk-distinct-aliases", source)
            assert observed.count(b"fs:walk-entry") == 4 and b"(fs:end 4) fs:closed" in observed, observed
            assert b'"first/leaf"' in observed and b'"second/leaf"' in observed, observed
            source = prefix + '(= (fs:walk-entry $p $r $n $h $i) (println! forbidden-walk))\n' \
                + f'!(let $owner (fs:walk-open {json.dumps(str(links))}) ' \
                + '(let $one (fs:walk-next $owner no-follow ()) ' \
                + '(let ($head $fields) (decons-atom $one) ' \
                + '(let $closed (fs:close $owner) (InertWalk $head $fields $closed)))))\n'
            observed = run(language, "walk-hostile-constructor", source)
            assert b"InertWalk fs:walk-entry" in observed, observed
        observed = run(language, "directory_entries_positive", path=nested)
        assert observed.count(b"fs:entry") == 2 and b'"first"' in observed and b'"second"' in observed, observed
        assert b"(fs:end 2)" in observed, observed
        if language != "prime":
            observed = run(language, "walk_batch_entries_positive", path=nested)
            assert b"BatchEntries 2 fs:closed" in observed, observed
            observed = run(language, "walk_batch_empty_positive", path=empty)
            assert b"BatchEmpty 0 (fs:end 0) fs:closed" in observed, observed
            observed = run(language, "walk_batch_fault_negative", path=nested)
            assert observed.count(b"BatchFaultKind fs:invalid-handle") == 1, observed
            assert b"BatchStopped -1" in observed and b"fs:end" not in observed, observed
        observed = run(language, "directory_invalid_negative", path=data)
        assert b"fs:failed fs:missing open-directory" in observed, observed
        observed = run(language, "bytes_exact_positive", path=data)
        assert observed.count(b"fs:chunk") == 2 and b"(fs:end 4)" in observed, observed
        observed = run(language, "bytes_zero_positive", path=data)
        assert b'(BytesZero (fs:chunk "" 0) (fs:chunk "a" 0))' in observed, observed
        observed = run(language, "bytes_fault_negative", path=empty)
        assert b"fs:failed fs:io read-chunk" in observed and b"unknown-position" in observed, observed
        observed = run(language, "lifetime_independent_positive", path=data)
        assert observed.count(b'(fs:chunk "a" 0)') == 2 and b"fs:failed" not in observed, observed
        observed = run(language, "lifetime_retained_positive", path=data)
        assert observed.count(b"fs:chunk") == 2 and b"fs:failed" not in observed, observed
        observed = run(language, "lifetime_closed_negative", path=data)
        assert b"fs:failed fs:invalid-handle" in observed, observed
        observed = run(language, "retained_bytes_closed_positive", path=large_data)
        assert b"RetainedBytesClosed 65536 fs:closed" in observed, observed
        observed = run(language, "retained_bytes_space_positive", path=large_data)
        assert b"RetainedBytesSpace 65536" in observed, observed
        observed = run(language, "retained_bytes_closed_negative", path=large_data)
        assert b"RetainedBytesFault 65536" in observed and b"fs:failed fs:invalid-handle" in observed, observed
        observed = run(language, "metadata_regular_positive", path=data)
        assert b"(fs:info regular 4 (fs:identity" in observed, observed
        observed = run(language, "metadata_links_positive", path=link)
        assert b"fs:info symlink" in observed and b"fs:info regular 4" in observed, observed
        os.utime(data, ns=(1_700_000_000_000_000_000, 1_700_000_000_123_456_789))
        observed = run(language, "metadata_modified_positive", path=data)
        assert b"(fs:modified 1700000000 123456789))" in observed, observed
        assert observed.count(b"(fs:info regular 4 (fs:identity") == 2, observed
        observed = run(language, "metadata_missing_negative", path=output / "absent")
        assert b"fs:failed fs:missing metadata" in observed, observed
        # A native answer must not run host equations that happen to share
        # the data constructor's spelling, including nested fields.
        source = prefix + '(= (fs:chunk $bytes $offset) (println! forbidden-chunk))\n' \
            + '(= (fs:progress $count $position) (println! forbidden-progress))\n' \
            + '(= (fs:end $offset) (println! forbidden-end))\n' \
            + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
            + '(let $value (fs:read-chunk $owner 1) (let $closed (fs:close $owner) ' \
            + '(function (return $value)))))\n' \
            + f'!(fs:metadata {json.dumps(str(output / "absent"))} follow)\n'
        assert b"fs:chunk" in run(language, "inert-native-data", source), source
        source = prefix + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
            + '(let $invalid (fs:read-chunk $owner -1) ' \
            + '(let $value (fs:read-chunk $owner 1) (let $closed (fs:close $owner) (AfterInvalid $invalid $value)))))\n'
        observed = run(language, "invalid-does-not-consume", source)
        assert b"fs:invalid-argument" in observed and b'(fs:chunk "a" 0)' in observed, observed
        observed = run(language, "entry-directory", prefix + '!(system:entry-directory)\n', cwd=empty)
        assert json.dumps(str(output)).encode() in observed, observed
        expected_entry = str(ROOT / "tests/filesystem_v1")
        observed = run(language, "entry_directory_positive", cwd=empty)
        assert json.dumps(expected_entry).encode() in observed, observed
        observed = run(language, "entry_imports_positive", cwd=empty)
        assert observed.count(json.dumps(expected_entry).encode()) == 2, observed
        observed = run(language, "entry_inline_negative", cwd=empty, inline=True)
        assert b"entry directory is not established" in observed, observed
        # Returning equal answers after the effect does not perform the read
        # twice. The next read starts at byte one.
        source = prefix + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
            + '(let $first (fs:read-chunk $owner 1) ' \
            + '(let $copies (collapse (superpose ($first $first))) ' \
            + '(let $next (fs:read-chunk $owner 1) ' \
            + '(let $closed (fs:close $owner) (ReadOnce $copies $next))))))\n'
        observed = run(language, "read-once-before-choice", source)
        assert observed.count(b'(fs:chunk "a" 0)') == 2 and b"fs:failed" not in observed, observed
        if language != "prime":
            source = prefix + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
                + '(let $work (work-new (superpose (1 2))) ' \
                + '(let $bound (work-budget $work 1) (let $paused (work-take $work 1) ' \
                + '(let $unbounded (work-budget $work 0) (let $resumed (work-take $work 3) ' \
                + '(let $value (fs:read-chunk $owner 1) (let $closed (fs:close $owner) ' \
                + '(let $cancelled (work-cancel $work) (PausedRead $paused $resumed $value))))))))))\n'
            observed = run(language, "pause-resume", source)
            assert b"(taken () paused) (taken (1 2) complete)" in observed, observed
            assert b'(fs:chunk "a" 0)' in observed, observed
            assert b"failed" not in observed, observed
            # Admission of retained effectful work is host-specific. A paused
            # or refused request must not consume a read ahead of its boundary.
            source = prefix + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
                + '(let $work (work-new (fs:read-chunk $owner 1)) ' \
                + '(let $bound (work-budget $work 1) (let $refused (work-take $work 1) ' \
                + '(let $value (fs:read-chunk $owner 1) (let $closed (fs:close $owner) ' \
                + '(let $cancelled (work-cancel $work) (UnsupportedRoute $refused $value))))))))\n'
            observed = run(language, "budgeted-read-boundary", source)
            if language == "he":
                assert b"WorkBudgetRequiresResumableRoute" in observed, observed
            else:
                assert b"(taken () paused)" in observed, observed
            assert b'(fs:chunk "a" 0)' in observed, observed
            if language == "petta":
                source = prefix + f'!(let $owner (fs:open-read {json.dumps(str(data))}) ' \
                    + '(let $work (work-new (fs:read-chunk $owner 1)) ' \
                    + '(let $bound (work-budget $work 1) (let $paused (work-take $work 1) ' \
                    + '(let $unbounded (work-budget $work 0) (let $resumed (work-take $work 2) ' \
                    + '(let $next (fs:read-chunk $owner 1) (let $closed (fs:close $owner) ' \
                    + '(let $cancelled (work-cancel $work) (ResumedRead $paused $resumed $next))))))))))\n'
                observed = run(language, "budgeted-read-resume", source)
                assert b"(taken () paused)" in observed and b'(fs:chunk "a" 0)' in observed, observed
                assert b"failed" not in observed and b' 1)' in observed, observed
    print(f"(FilesystemV1Summary {count} 0)")


if __name__ == "__main__":
    main()

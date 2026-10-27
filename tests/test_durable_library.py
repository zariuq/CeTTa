#!/usr/bin/env python3
"""Exercise the real MeTTa boundary in fresh, private local databases."""
import pathlib
import re
from support.durable_journal_fixture import damage_value
import subprocess
import sys
import tempfile


def run(binary, language, directory, source, normalize=True, admin=True):
    program = directory / "probe.metta"
    program.write_text('!(import! &self durable)\n' + source)
    arguments = [binary, "--lang", language]
    if admin:
        arguments.append("--durable-admin")
    result = subprocess.run(arguments + [str(program)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (language, result.returncode, result.stderr, result.stdout)
    assert not result.stderr.strip(), result.stderr
    lines = result.stdout.strip().splitlines()
    if language == "he":
        assert all(line.startswith("[") and line.endswith("]") for line in lines), lines
        lines = [line[1:-1] for line in lines]
    assert lines.pop(0) in ("true", "()"), lines
    if not normalize:
        return lines
    return [re.sub(r'"[0-9a-f]{32}"', '"EPOCH"', line) for line in lines]


def commit(operations, revision, epoch):
    return f'!(durable:commit "{epoch}" {revision} ({operations}))\n'


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    for language in ("petta", "he"):
        with tempfile.TemporaryDirectory(prefix="cetta-durable-library-") as temporary:
            directory = pathlib.Path(temporary)
            opening = '!(durable:open "' + str(directory / "coordination.db") + '")\n'
            # Editable bootstrap files are ordinary programs. Merely passing
            # them on the command line must not create a durable store.
            denied = '(durable:failure commit-boundary-required)'
            assert run(binary, language, directory, opening, admin=False) == [denied]
            assert not (directory / "coordination.db").exists()
            seed = run(binary, language, directory, opening + '!(durable:close)\n', normalize=False)
            epoch = re.search(r'"([0-9a-f]{32})"', seed[0]).group(1)
            source = opening
            source += commit('''(durable:insert "inbox" "one" (message "hello"))
              (durable:insert "state" "worker" (waiting 0))''', 0, epoch)
            source += commit('''(durable:remove "inbox" "one")
              (durable:replace "state" "worker" (continuation 1))
              (durable:insert "outbox" "send:1" (send "test" "reply"))
              (durable:insert "data" "inert" (println! "DO_NOT_EXECUTE"))''', 1, epoch)
            source += commit('(durable:insert "outbox" "stale" ignored)', 0, epoch)
            source += commit('''(durable:replace "state" "worker" wrong)
              (durable:remove "inbox" "missing")''', 2, epoch)
            source += '!(durable:read "state")\n!(durable:checkpoint)\n!(durable:close)\n'
            assert run(binary, language, directory, source) == [
                '(durable:snapshot "EPOCH" 0 ())',
                '(durable:committed "EPOCH" 1)', '(durable:committed "EPOCH" 2)',
                '(durable:failure conflict)', '(durable:failure precondition)',
                '(durable:snapshot "EPOCH" 2 ((durable:record "state" "worker" (continuation 1))))',
                '(durable:checkpointed)', '(durable:closed)',
            ]
            # A new process recovers data without executing the saved expression.
            lines = run(binary, language, directory,
                        opening + '!(durable:read "data")\n!(durable:close)\n')
            assert len(lines) == 3, lines
            assert lines[0] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "state" "worker" (continuation 1)) '
                '(durable:record "outbox" "send:1" (send "test" "reply")) '
                '(durable:record "data" "inert" (println! "DO_NOT_EXECUTE"))))'), lines
            assert lines[1] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "data" "inert" (println! "DO_NOT_EXECUTE"))))'), lines
            assert lines[2] == '(durable:closed)', lines
            attempt = f'(durable:commit "{epoch}" 2 ((durable:insert "outbox" "forbidden" hidden)))'
            # Even literal root directives with the correct epoch and revision
            # require the opt-in. Deny reads and maintenance as well as writes.
            assert run(binary, language, directory, opening + f'!{attempt}\n'
                       '!(durable:read "outbox")\n!(durable:checkpoint)\n!(durable:close)\n',
                       admin=False) == [denied] * 5
            lines = run(binary, language, directory, opening +
                        '!(durable:read "outbox")\n!(durable:close)\n')
            assert lines[1] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "outbox" "send:1" (send "test" "reply"))))'), lines
            probes = [
                f'(superpose ({attempt} {attempt}))',
                f'(collapse {attempt})',
                '(hidden-commit)',
                f'(eval {attempt})',
                attempt.replace('durable:commit', '__cetta_lib_durable_commit'),
                '(superpose ((durable:close) (durable:checkpoint)))',
                '(collapse (durable:read "*"))',
            ]
            if language == "he":
                probes.append(f'(metta {attempt} %Undefined% &self)')
            for probe in probes:
                source = opening + f'(= (hidden-commit) {attempt})\n!{probe}\n!(durable:read "outbox")\n!(durable:close)\n'
                lines = run(binary, language, directory, source)
                if language == "he" and probe.startswith("(eval "):
                    # HE eval takes one step, exposing the inert native request;
                    # the full interpreter below must still refuse its execution.
                    expected = attempt.replace("durable:commit", "__cetta_lib_durable_commit").replace(epoch, "EPOCH")
                    assert lines[1] == expected, (language, probe, lines)
                else:
                    assert any('commit-boundary-required' in line for line in lines), (language, probe, lines)
                assert lines[-2] == ('(durable:snapshot "EPOCH" 2 ('
                    '(durable:record "outbox" "send:1" (send "test" "reply"))))'), lines
                assert lines[-1] == '(durable:closed)', lines
            # The raw batch boundary must publish completed refusal as data,
            # preserve its environment, and never execute a stored operand.
            source = '!(durable:commit "no-authority" 0 ((durable:insert "state" "raw" (println! "BATCH_EXECUTED"))))\n'
            lines = run(binary, language, directory, source, admin=False)
            assert lines[-1] == denied and not any("BATCH_EXECUTED" in line for line in lines), lines
            # COMM executes a deferred payload in its own evaluator invocation.
            # It must not gain the CLI's administrative authority.
            source = opening + '!(import! &self rhometta)\n'
            source += f'''!(rhometta:run-canonical
              (rho:par
                (rho:send (rho:quote rho:nil) (rhometta:eval {attempt}))
                (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))
              !(durable:read "outbox")
              !(durable:close)
'''
            lines = run(binary, language, directory, source)
            assert any('commit-boundary-required' in line for line in lines), lines
            assert lines[-2] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "outbox" "send:1" (send "test" "reply"))))'), lines
            imported = directory / "attempt.metta"
            imported.write_text(f'!{attempt}\n!(durable:close)\n')
            lines = run(binary, language, directory, opening +
                        f'!(import! &self "{imported}")\n!(durable:read "outbox")\n!(durable:close)\n')
            assert lines[-2] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "outbox" "send:1" (send "test" "reply"))))'), lines
            # A well-framed but invalid application codec must identify the
            # damaged occurrence without interpreting it or hiding other rows.
            damage_value(directory / "coordination.db", "inert")
            lines = run(binary, language, directory, opening + '!(durable:read "state")\n!(durable:close)\n')
            assert lines[0] == '(durable:failure corrupt "data" "inert")', lines
            assert lines[1] == ('(durable:snapshot "EPOCH" 2 ('
                '(durable:record "state" "worker" (continuation 1))))'), lines
    print("durable library: PeTTa and HE, explicit admin opt-in, atomic input/state/outbox, stale reads, rollback, inert data, process recovery, evaluator and rho isolation passed")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Private ACT artifacts and atomic disk-backed remote admission."""
import argparse
import hashlib
import json
import stat
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from check_native_db_server import Client, E, S, capture, cursor, drain, server_for, DropReplyProxy

ROOT = Path(__file__).resolve().parents[2]


def native_client(args, source):
    prefix = []
    if args.valgrind_log_dir:
        prefix = ['valgrind', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect',
                  '--error-exitcode=99', '--log-file='+str(args.valgrind_log_dir.resolve()/'client-%p.log')]
    return prefix+[str(args.binary.resolve()), '--lang', 'petta', str(source)]


def process_memory(pid):
    try:
        fields = dict(line.split(':', 1) for line in (Path('/proc')/str(pid)/'status').read_text().splitlines() if ':' in line)
        return {key: int(fields.get(key, '0').split()[0]) for key in ['VmRSS', 'RssAnon', 'RssFile']}
    except (FileNotFoundError, ProcessLookupError):
        return {}


def private_file_bytes(pid):
    seen, total = set(), 0
    try:
        for path in (Path('/proc')/str(pid)/'fd').iterdir():
            try:
                metadata = path.stat()
                if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 0:
                    continue
                identity = (metadata.st_dev, metadata.st_ino)
                if identity not in seen:
                    seen.add(identity)
                    total += metadata.st_size
            except (FileNotFoundError, PermissionError):
                pass
    except (FileNotFoundError, PermissionError):
        return None
    return total


def scaling(args, root, rows):
    root.mkdir(mode=0o700)
    image = root/'input.cact'
    subprocess.run([str(args.act_fixture.resolve()), 'generate', str(image), str(rows), '512'], check=True, capture_output=True)
    server = server_for(args.worker.resolve(), root, root/'store')
    peaks = {'client': {}, 'server': {}}
    temporary, unavailable = 0, 0
    client = None
    try:
        server.start()
        source = root/'client.metta'
        output = root/'output.cact'
        source.write_text('!(import! &self (library db))\n!(import! &self (library lib_he))\n'
            + '(= (artifact-scale:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))'
            + ' ($_ (assertEqual (db:import-act-result $db data scale-import copy '+json.dumps(str(image))+') (db:committed scale-import (True))))'
            + ' ($_ (assertEqual (db:export-act $db '+json.dumps(str(output))+') True))'
            + ' ($_ (db:close $db))) ARTIFACT_SCALE_OK))\n!(println! (artifact-scale:run))\n')
        began = time.monotonic()
        with open(root/'client.log', 'w') as log:
            client = subprocess.Popen([str(args.binary.resolve()), '--lang', 'petta', str(source)], stdout=log, stderr=subprocess.STDOUT)
            while client.poll() is None:
                for label, pid in [('client', client.pid), ('server', server.process.pid)]:
                    for field, value in process_memory(pid).items():
                        peaks[label][field] = max(peaks[label].get(field, 0), value)
                client_disk, server_disk = private_file_bytes(client.pid), private_file_bytes(server.process.pid)
                if client_disk is not None and server_disk is not None:
                    temporary = max(temporary, client_disk+server_disk)
                else:
                    unavailable += 1
                if time.monotonic()-began > 600:
                    client.kill()
                    client.wait()
                    raise AssertionError(('scaling timeout', rows))
                time.sleep(.02)
        log = (root/'client.log').read_text()
        assert client.returncode == 0 and 'ARTIFACT_SCALE_OK' in log.splitlines(), (rows, client.returncode, log)
        # Native validation checks every posting and complete occurrence stream.
        validation = root/'validate.metta'
        validation.write_text('!(import! &self (library act))\n'
            + '!(println! (act:info (act:open '+json.dumps(str(output))+')))\n')
        result = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(validation)], capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and '(ActSnapshotInfo CACT0001 '+str(rows)+' ' in result.stdout, (result.stdout, result.stderr)
        verified = subprocess.run([str(args.act_fixture.resolve()), 'verify-generated', str(output), str(rows), '512'],
                                  check=True, capture_output=True, text=True, timeout=120)
        assert 'ACT_VERIFIED_GENERATED rows='+str(rows) in verified.stdout, verified.stdout
        return dict(rows=rows, input_bytes=image.stat().st_size, output_bytes=output.stat().st_size,
                    seconds=time.monotonic()-began, peak_kib=peaks, sampled_private_file_bytes=temporary,
                    private_file_unavailable_samples=unavailable)
    finally:
        if client is not None and client.poll() is None:
            client.kill()
            client.wait()
        server.stop()


def ready(client, token):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        value = client.route('DbArtifactStep', E('DbArtifactStepV1', 65536), token)
        assert value[0] == S('ActArtifactInfoV1'), value
        if value[1] == S('ready'):
            return value
        assert value[1] in [S('building'), S('hashing'), S('validating')], value
    raise AssertionError('artifact did not complete')


def upload(client, data, digest=None):
    answer = client.route('DbUploadOpen', E('DbUploadOpenV1', len(data), digest or hashlib.sha256(data).hexdigest()))
    assert answer[0] == S('ServiceResourceV1'), answer
    token = answer[1]
    for offset in range(0, len(data), 8192):
        value = client.route('DbArtifactWrite', E('DbArtifactWriteV1', offset, data[offset:offset+8192]), token)
        assert value[:3] == (S('ActArtifactInfoV1'), S('receiving'), min(offset+8192, len(data))), value
    return token


def continuation(client, token, operation, argument=()):
    return client.call(E('UseValueV1', token, S(operation), argument))


def fault_controls(args, root):
    if not sys.platform.startswith('linux') or args.valgrind_log_dir:
        return []
    library = root/'artifact-fault.so'
    subprocess.run(['cc', '-shared', '-fPIC', str(ROOT/'tests/support/artifact_io_fault.c'), '-ldl', '-o', str(library)], check=True)
    image = root/'fault-input.cact'
    subprocess.run([str(args.act_fixture.resolve()), 'generate', str(image), '256', '512'], check=True, capture_output=True)
    data = image.read_bytes()
    checks = []
    for point in [1, 10]:
        directory = root/('fault-'+str(point))
        directory.mkdir(mode=0o700)
        arm, marker = directory/'armed', directory/'injected'
        server = server_for(args.worker.resolve(), directory, directory/'store')
        server.environment.update(LD_PRELOAD=str(library), CETTA_ARTIFACT_FAULT_DIRECTORY=str(directory),
                                  CETTA_ARTIFACT_FAULT_AT=str(point), CETTA_ARTIFACT_FAULT_ARM=str(arm),
                                  CETTA_ARTIFACT_FAULT_MARKER=str(marker))
        client = None
        try:
            server.start()
            arm.touch()
            client = Client(server.path)
            token = client.route('DbUploadOpen', E('DbUploadOpenV1', len(data), hashlib.sha256(data).hexdigest()))[1]
            for offset in range(0, len(data), 8192):
                value = client.route('DbArtifactWrite', E('DbArtifactWriteV1', offset, data[offset:offset+8192]), token)
                if value[0] == S('DbErrorV1'):
                    break
            assert marker.exists() and value[0] == S('DbErrorV1'), (point, value)
            assert client.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('failed-import'), S('copy')), token)[0] == S('DbErrorV1')
            view, metadata = capture(client)
            scan = cursor(client, view)
            assert metadata[5] == 0 and drain(client, scan) == []
            client.drop(scan)
            client.drop(view)
            client.drop(token)
            checks.append('upload_io_failure_'+str(point))
        finally:
            if client:
                client.close()
            server.stop()
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('worker', type=Path)
    parser.add_argument('act_fixture', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--scale-rows', type=int, nargs='*', default=[])
    parser.add_argument('--valgrind-log-dir', type=Path)
    args = parser.parse_args()
    if args.valgrind_log_dir:
        args.valgrind_log_dir.mkdir(parents=True, exist_ok=True)
    checks = []
    with tempfile.TemporaryDirectory(prefix='db-artifacts-', dir=ROOT/'runtime') as temporary:
        root = Path(temporary)
        image = root/'source.cact'
        subprocess.run([str(args.act_fixture.resolve()), 'write', str(image)], check=True, capture_output=True)
        data = image.read_bytes()
        server = server_for(args.worker.resolve(), root, root/'store')
        if args.valgrind_log_dir:
            server.command[:0] = ['valgrind', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect',
                                  '--error-exitcode=99', '--log-file='+str(args.valgrind_log_dir.resolve()/'server-%p.log')]
        first = second = None
        try:
            server.start()
            first, second = Client(server.path), Client(server.path)
            initial_private_bytes = private_file_bytes(server.process.pid)
            assert first.route('DbTransferInfo')[0] == S('DbTransferV1')
            token = upload(first, data)
            # Retries compare actual retained bytes; overlapping mutations and
            # gaps refuse without changing the admitted upload prefix.
            assert first.route('DbArtifactWrite', E('DbArtifactWriteV1', 0, data[:20]), token)[0] == S('ActArtifactInfoV1')
            assert first.route('DbArtifactWrite', E('DbArtifactWriteV1', 0, b'changed'), token)[0] == S('DbErrorV1')
            assert second.route('DbArtifactInfo', (), token, ok=False) == E('ServiceRefusedV1', S('unknown-resource'))
            info = ready(first, token)
            assert info[3:5] == (len(data), hashlib.sha256(data).hexdigest()), info
            assert first.route('DbArtifactWrite', E('DbArtifactWriteV1', 0, data[:20]), token)[0] == S('DbErrorV1')
            checks.append('private_upload_replay_and_seal')
            source, _ = capture(first)
            scan = cursor(first, source)
            assert drain(first, scan) == []
            first.drop(scan)
            first.drop(source)
            opened = first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('raw-import'), S('copy')), token)
            assert opened[0] == S('ServiceResourceV1') and opened[2][0] == S('DbLinearV1'), opened
            state = opened[1]
            first.drop(token)
            for step_number in range(30):
                old = state
                value = continuation(first, state, 'DbImportNext', E('DbImportNextV1', 3))
                assert value[0] == S('ServiceResourceV1'), value
                state = value[1]
                progress = value[2][1]
                assert progress[0] == S('DbImportProgressV1'), progress
                assert first.call(E('UseValueV1', old, S('DbImportNext'), E('DbImportNextV1', 1)), ok=False) == E('ServiceRefusedV1', S('unknown-resource'))
                observed, _ = capture(second)
                scan = cursor(second, observed)
                assert drain(second, scan) == []
                second.drop(scan)
                second.drop(observed)
                if progress[1] is True:
                    break
            committed = continuation(first, state, 'DbImportFinish')
            assert committed[0] == S('KnowledgeCommittedV1'), committed
            observed, metadata = capture(second)
            scan = cursor(second, observed)
            rows = drain(second, scan)
            assert len(rows) == 17 and len({repr(row[1]) for row in rows}) == 17
            second.drop(scan)
            second.drop(observed)
            checks.append('linear_bounded_atomic_import')
            # Captured export remains unchanged while a newer import commits.
            exported = first.route('DbExportOpen', E('DbExportOpenV1', S('data'), 1048576))
            assert exported[0] == S('ServiceResourceV1'), exported
            artifact = exported[1]
            upload2 = upload(first, data)
            ready(first, upload2)
            opened = first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('cancel-import'), S('copy')), upload2)
            state = opened[1]
            value = continuation(first, state, 'DbImportNext', E('DbImportNextV1', 2))
            assert continuation(first, value[1], 'DbImportCancel') == S('DbImportCancelledV1')
            # A view captured for admission cannot be silently refreshed.
            opened = first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('stale-import'), S('copy')), upload2)
            stale = opened[1]
            opened = first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('second-import'), S('copy')), upload2)
            state = opened[1]
            for _ in range(30):
                value = continuation(first, state, 'DbImportNext', E('DbImportNextV1', 3))
                assert value[0] == S('ServiceResourceV1'), value
                state = value[1]
                if value[2][1][1] is True:
                    break
            assert continuation(first, state, 'DbImportFinish')[0] == S('KnowledgeCommittedV1')
            value = continuation(first, stale, 'DbImportNext', E('DbImportNextV1', 32))
            assert value[0] == S('ServiceResourceV1'), value
            assert continuation(first, value[1], 'DbImportFinish') == E('StoreRefused', S('conflict'))
            first.drop(upload2)
            info = ready(first, artifact)
            received = bytearray()
            while len(received) < info[3]:
                answer = first.route('DbArtifactRead', E('DbArtifactReadV1', len(received), 4096), artifact)
                assert answer[:2] == (S('ActArtifactChunkV1'), len(received)), answer
                received.extend(answer[2] if isinstance(answer[2], bytes) else answer[2].encode())
            assert hashlib.sha256(received).hexdigest() == info[4]
            output = root/'export.cact'
            output.write_bytes(received)
            subprocess.run([str(args.act_fixture.resolve()), 'verify', str(output)], check=True, capture_output=True)
            first.drop(artifact)
            checks.append('captured_export_and_cancelled_admission')
            broken = upload(first, data, '0'*64)
            deadline = time.monotonic()+20
            while True:
                value = first.route('DbArtifactStep', E('DbArtifactStepV1', 65536), broken)
                if value[0] == S('DbErrorV1'):
                    break
                assert time.monotonic() < deadline
            assert first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('broken'), S('copy')), broken)[0] == S('DbErrorV1')
            first.drop(broken)
            checks.append('digest_failure_never_publishes')
            malformed = upload(first, b'CACT0001'+b'not a valid image')
            for _ in range(30):
                value = first.route('DbArtifactStep', E('DbArtifactStepV1', 65536), malformed)
                if value[0] == S('DbErrorV1'):
                    break
            assert value[0] == S('DbErrorV1'), value
            first.drop(malformed)
            checks.append('structure_failure_with_matching_digest')
            incomplete = first.route('DbUploadOpen', E('DbUploadOpenV1', len(data), hashlib.sha256(data).hexdigest()))[1]
            assert first.route('DbArtifactWrite', E('DbArtifactWriteV1', 1, data[:5]), incomplete)[0] == S('DbErrorV1')
            assert first.route('DbArtifactStep', E('DbArtifactStepV1', 0), incomplete)[1:3] == (S('receiving'), 0)
            assert first.route('DbImportOpen', E('DbImportOpenV1', S('data'), S('incomplete'), S('copy')), incomplete)[0] == S('DbErrorV1')
            first.drop(incomplete)
            checks.append('incomplete_and_out_of_order_upload')
            # The ordinary MeTTa facade uses the native client and writes only
            # its local destination. Exact-request retry must replay receipts.
            client = root/'client.metta'
            local = root/'client-output.cact'
            client.write_text('!(import! &self (library db))\n!(import! &self (library lib_he))\n'
                + '(= (artifact-test:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))\n'
                + '($first (db:import-act-result $db data facade-import copy '+json.dumps(str(image))+'))\n'
                + '($_ (println! $first)) ($_ (assertEqual $first (db:committed facade-import (True))))\n'
                + '($_ (assertEqual (db:import-act-result $db data facade-import copy '+json.dumps(str(image))+') (db:replayed facade-import (True))))\n'
                + '($export (db:export-act $db '+json.dumps(str(local))+'))\n'
                + '($_ (println! $export)) ($_ (assertEqual $export True))\n'
                + '($_ (assertEqual (size-atom (collapse (get-atoms (act:open '+json.dumps(str(local))+')))) 51))\n'
                + '($_ (db:close $db))) ARTIFACT_FACADE_OK))\n!(println! (artifact-test:run))\n')
            result = subprocess.run(native_client(args, client), capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and 'ARTIFACT_FACADE_OK' in result.stdout.splitlines(), (result.returncode, result.stdout, result.stderr)
            assert 'Assertion failed' not in result.stdout+result.stderr, (result.stdout, result.stderr)
            checks.append('ordinary_remote_facade_and_receipt_replay')
            # Discard a fully observed commit acknowledgement. The client
            # reports uncertainty, and restart/retry resolves one saved result.
            proxy = DropReplyProxy(root/'lost.sock', server.path, S('DbImportFinish'))
            lost = root/'lost.metta'
            lost.write_text('!(import! &self (library db))\n'
                + '(= (artifact-lost:run) (let* (($db (db:connect '+json.dumps(str(proxy.path))+'))'
                + ' ($result (db:import-act-result $db data lost-import copy '+json.dumps(str(image))+'))'
                + ' ($_ (db:close $db))) $result))\n!(println! (artifact-lost:run))\n')
            result = subprocess.run(native_client(args, lost), capture_output=True, text=True, timeout=120)
            proxy.join()
            assert proxy.observed.is_set() and result.returncode == 0 and '(db:uncertain lost-import data (DbImportV1 copy CACT0001 ' in result.stdout, (result.stdout, result.stderr)
            first.close()
            first = None
            second.close()
            second = None
            server.stop(crash=True)
            server.start()
            replay = root/'replay.metta'
            replay.write_text('!(import! &self (library db))\n!(import! &self (library lib_he))\n'
                + '(= (artifact-replay:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))'
                + ' ($_ (assertEqual (db:import-act-result $db data lost-import copy '+json.dumps(str(image))+') (db:replayed lost-import (True))))'
                + ' ($_ (assertEqual (size-atom (collapse (get-atoms (db:snapshot $db)))) 68))'
                + ' ($_ (db:close $db))) ARTIFACT_REPLAY_OK))\n!(println! (artifact-replay:run))\n')
            result = subprocess.run(native_client(args, replay), capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and 'ARTIFACT_REPLAY_OK' in result.stdout.splitlines(), (result.stdout, result.stderr)
            checks.append('lost_admission_reply_restart_and_receipt_resolution')
            readonly = Client(server.path)
            readonly.call(E('ReadOnlyV1'))
            assert readonly.route('DbUploadOpen', E('DbUploadOpenV1', len(data), hashlib.sha256(data).hexdigest()), ok=False) == E('ServiceRefusedV1', S('read-only'))
            readonly.close()
            checks.append('read_only_attenuation')
            # All paths used for construction are anonymous files. Closing
            # sessions must return both service entries and scratch FDs.
            observer = Client(server.path)
            try:
                deadline = time.monotonic()+5
                while observer.call(E('StatsV1'))[2] != 0:
                    assert time.monotonic() < deadline
                    time.sleep(.01)
                for fd in (Path('/proc')/str(server.process.pid)/'fd').iterdir():
                    try:
                        target = str(fd.readlink())
                        assert '.act-scratch-' not in target and '.act-publish-' not in target, target
                    except FileNotFoundError:
                        pass
                assert private_file_bytes(server.process.pid) == initial_private_bytes, 'anonymous files survived session cleanup'
            finally:
                observer.close()
            checks.append('session_cleanup_releases_private_files')
        finally:
            if first:
                first.close()
            if second:
                second.close()
            server.stop()
        readonly_directory = root/'act-server'
        readonly_directory.mkdir(mode=0o700)
        readonly_server = server_for(args.worker.resolve(), readonly_directory, image)
        try:
            readonly_server.start()
            source = readonly_directory/'client.metta'
            exported = readonly_directory/'export.cact'
            source.write_text('!(import! &self (library db))\n!(import! &self (library lib_he))\n'
                + '(= (artifact-act:run) (let* (($db (db:connect '+json.dumps(str(readonly_server.path))+'))'
                + ' ($_ (assertEqual (db:import-act-result $db data denied copy '+json.dumps(str(image))+') (Error db:import read-only)))'
                + ' ($_ (assertEqual (db:export-act $db '+json.dumps(str(exported))+') True))'
                + ' ($_ (assertEqual (size-atom (collapse (get-atoms (act:open '+json.dumps(str(exported))+')))) 17))'
                + ' ($_ (db:close $db))) ARTIFACT_ACT_OK))\n!(println! (artifact-act:run))\n')
            result = subprocess.run(native_client(args, source), capture_output=True, text=True, timeout=120)
            assert result.returncode == 0 and 'ARTIFACT_ACT_OK' in result.stdout.splitlines(), (result.stdout, result.stderr)
            checks.append('standalone_act_export_and_read_only_import')
        finally:
            readonly_server.stop()
        checks.extend(fault_controls(args, root))
        scales = [scaling(args, root/('scale-'+str(rows)), rows) for rows in args.scale_rows]
    receipt = dict(checks=checks, passed=len(checks), scaling=scales)
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()

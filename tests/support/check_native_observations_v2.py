"""Lossless TOML and versioned file observations through actual native calls."""
import argparse, datetime, hashlib, json, math, os, struct, sys, tempfile, tomllib
from contextlib import closing
from pathlib import Path
r = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(r / 'tools'))
from value_worker_client import Worker
from space_service_client import E, S, FloatBits, decode
p = argparse.ArgumentParser()
p.add_argument('worker', type=Path)
p.add_argument('--binary', type=Path)
a = p.parse_args()

def temporal(value):
    if isinstance(value, datetime.datetime):
        return E('TomlTemporalV1', S('datetime-offset' if value.tzinfo else 'datetime'), value.isoformat())
    if isinstance(value, datetime.date):
        return E('TomlTemporalV1', S('date'), value.isoformat())
    if isinstance(value, datetime.time):
        return E('TomlTemporalV1', S('time'), value.isoformat())
    if isinstance(value, dict):
        return E('TomlTableV1', tuple((E('TomlFieldV1', k, temporal(v)) for k, v in value.items())))
    if isinstance(value, list):
        return E('TomlArrayV1', tuple(map(temporal, value)))
    if isinstance(value, float):
        return FloatBits(struct.unpack('<Q', struct.pack('<d', value))[0])
    return value

def equal(a, b):
    if isinstance(a, FloatBits) and isinstance(b, FloatBits):
        av = struct.unpack('<d', struct.pack('<Q', a.bits))[0]
        bv = struct.unpack('<d', struct.pack('<Q', b.bits))[0]
        return math.isnan(av) and math.isnan(bv) or a.bits == b.bits
    if isinstance(a, tuple) and isinstance(b, tuple):
        return len(a) == len(b) and all((equal(x, y) for x, y in zip(a, b)))
    return type(a) == type(b) and a == b
results = []
for lang in ('petta',):
    with closing(Worker(str(a.worker), r / 'tests/support/native_observation_values.metta', 'probe:dispatch', language=lang, import_mode='relative')) as worker:
        corpus = r / 'tests/support/fixtures/toml-test'
        count = 0
        for name in (corpus / 'files-toml-1.1.0').read_text().splitlines():
            if not name.startswith('valid/') or not name.endswith('.toml'):
                continue
            text = (corpus / name).read_text()
            try:
                expected = temporal(tomllib.loads(text))
            except tomllib.TOMLDecodeError:
                continue
            actual = worker.call(E('Toml', text))
            assert equal(actual, expected), (lang, name, actual, expected)
            count += 1
        with tempfile.TemporaryDirectory(prefix='native-file-observer-') as directory:
            root = Path(directory)
            for data in [b'', b'abc', bytes(range(256)) * 257, b'\x00' * 65537]:
                file = root / 'binary'
                file.write_bytes(data)
                reply = worker.call(E('Hash', str(file)))
                assert reply[0] == S('fs:hashed-v1'), reply
                assert reply[1] == hashlib.sha256(data).hexdigest(), reply
                info = reply[2]
                assert info[0] == S('fs:info-v2') and info[2] == len(data), info
                assert info[7] == E('fs:blocks', file.stat().st_blocks), info
            for name in [str(root / 'missing'), str(root), str(root / 'binary') + '\x00trailing', '/proc/self/cmdline']:
                reply = worker.call(E('Hash', name))
                assert reply[0] == S('fs:failed'), (name, reply)
            fifo = root / 'fifo'
            os.mkfifo(fifo)
            assert worker.call(E('Hash', str(fifo)))[0] == S('fs:failed')
        results.append(dict(language=lang, toml_documents=count, file_controls=9))
        print(json.dumps(results[-1]), flush=True)
if a.binary:
    with tempfile.TemporaryDirectory(prefix='native-observation-he-') as directory:
        folder = Path(directory)
        cases = ['empty=[]\nflag=false\n', 'n=nan\ni=inf\nz=-0.0\n', 'date=2026-11-12\ntime=01:02:03.123456\ndt=2026-11-12T01:02:03Z\n', "x=[1, 1, 'λ']\n"]
        source = '!(import! &self (library toml))\n!(import! &self (library store))\n!(import! &self (library fs))\n'
        source += '(= (save $path $value) (let (StoreEncoded $bytes) (store:encode $value) (let $writer (fs:open-write $path ("create" "truncate") 384) (let* (($written (collapse (fs:write-all $writer $bytes))) ($closed (collapse (fs:close $writer)))) True))))\n'
        for i, text in enumerate(cases):
            source += '!(save ' + json.dumps(str(folder / str(i))) + ' (__cetta_lib_toml parse-value (' + json.dumps(text, ensure_ascii=False) + ')))\n'
        program = folder / 'probe.metta'
        program.write_text(source)
        import subprocess
        process = subprocess.run([str(a.binary), '--lang', 'he', '--profile', 'extended', '--no-results', str(program)], cwd=r, capture_output=True, timeout=30)
        assert process.returncode == 0, process.stderr
        for i, text in enumerate(cases):
            assert equal(decode((folder / str(i)).read_bytes()), temporal(tomllib.loads(text))), (i, process.stdout)
        results.append(dict(language='he', toml_documents=len(cases)))
        print(json.dumps(results[-1]))
print(json.dumps(dict(status='passed', observations=results)))

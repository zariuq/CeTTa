"""Independent hashlib/Unicode controls for the native text operations."""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from value_worker_client import Worker
from space_service_client import E, S


def chain(seed, records):
    state = hashlib.sha256(seed.encode())
    initial = state.hexdigest()
    values = []
    for record in records:
        data = record.encode()
        state.update(len(data).to_bytes(8, 'big'))
        state.update(data)
        values.append((len(data), hashlib.sha256(data).hexdigest(), state.hexdigest()))
    return E('Sha256ChainV1', initial, tuple(values))


def literal(text):
    if any(ord(c) < 32 and c not in '\b\f\n\r\t' for c in text):
        return '(str:from-codepoints (%s))' % ' '.join(str(ord(c)) for c in text)
    return json.dumps(text, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--he-cetta', type=Path)
    parser.add_argument('--cases', type=int, default=1500)
    args = parser.parse_args()
    rng = random.Random(718021)
    chars = 'abλβ🌿\n\t \x00\x1c\u0085\u2003\u2028\u3000'
    vectors = [('Hash', '', ()), ('Hash', 'seed', ('a', 'a', '', '\x00')),
               ('Hash', '', ('ab', 'c')), ('Hash', '', ('a', 'bc'))]
    for index in range(args.cases):
        text = ''.join(rng.choices(chars, k=rng.randrange(35)))
        if index % 2:
            records = tuple(''.join(rng.choices(chars, k=rng.randrange(40))) for _ in range(rng.randrange(12)))
            vectors.append(('Hash', text, records))
        else:
            trim = None if index % 4 == 0 else ''.join(rng.choices(chars, k=rng.randrange(12)))
            vectors.append(('Strip', text, trim))
    with closing(Worker(str(args.worker.resolve()), ROOT / 'tests/support/text_hash_probe.metta',
                        'text-hash:probe', language='petta', import_mode='relative')) as worker:
        for kind, text, other in vectors:
            expected = chain(text, other) if kind == 'Hash' else text.strip(other)
            argument = S('PythonWhitespace') if other is None else other
            actual = worker.call(E(kind, text, argument))
            assert actual == expected, (kind, text, other, expected, actual)
    he_count = 0
    if args.he_cetta:
        forms = ['!(import! &self (library str))']
        for index, (kind, text, other) in enumerate(vectors[:100]):
            if kind == 'Hash':
                initial, rows = chain(text, other)[1:]
                expected = '(%s)' % ' '.join('(%d %s %s)' % (n, literal(digest), literal(prefix)) for n, digest, prefix in rows)
                expression = '(case (str:sha256-chain %s (%s)) (((Sha256ChainV1 $initial $rows) (and (== $initial %s) (== $rows %s))) ($other False)))' % (
                    literal(text), ' '.join(literal(t) for t in other), literal(initial), expected)
            else:
                call = '(str:strip-python %s%s)' % (literal(text), '' if other is None else ' ' + literal(other))
                expression = '(== %s %s)' % (call, literal(text.strip(other)))
            forms.append('!(println! (TEXT-CONTROL-%d %s))' % (index, expression))
        with tempfile.TemporaryDirectory(prefix='cetta-text-hash-') as temporary:
            entry = Path(temporary) / 'controls.metta'
            entry.write_text('\n'.join(forms) + '\n')
            result = subprocess.run([str(args.he_cetta.resolve()), '--lang', 'he', '--profile', 'extended',
                                     '--quiet', str(entry)], cwd=ROOT, text=True, capture_output=True, timeout=60)
            assert result.returncode == 0, result.stderr[-2000:]
            for index in range(len(forms) - 1):
                assert '(TEXT-CONTROL-%d True)' % index in result.stdout, (index, result.stdout[-2500:])
                he_count += 1
    print(json.dumps(dict(status='passed', native_vectors=len(vectors), he_vectors=he_count)))


if __name__ == '__main__':
    main()

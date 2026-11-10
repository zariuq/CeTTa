#!/usr/bin/env python3
"""The public HE and PeTTa wrappers preserve the text envelope contract."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
cases = [
    ('(batch (a) (a))', 64, '((a) (a))'),
    ('(batch (run (batch (data))))', 64, '((run (batch (data))))'),
    ('(batch (send "λ😀\\\"(b)"))', 64, '((send "λ😀\\\"(b)"))'),
    ('(batch (send "a\x00b"))', 64, '((send "a\x00b"))'),
    ('(batch\u2005(a)\u3000(b))', 64, '((a) (b))'),
    ('(batch (a)) (open', 64, '(batch (a)) (open'),
    ('(batch (batch (a)))', 1, '((batch (a)))'),
    ('(batch (a))', 0, '(batch (a))'),
]
source = ['!(import! &self (library str))']
for text, depth, expected in cases:
    codes = ' '.join(str(ord(c)) for c in text)
    source.append('!(println! (str:sha256 (str:unwrap-envelopes (str:from-codepoints (' +
                  codes + ')) "batch" ' + str(depth) + ')))')
for dialect in ('petta', 'he'):
    for reference in ('0', '1'):
        result = subprocess.run([binary, '--lang', dialect, '--profile', 'extended',
                                 '-e', '\n'.join(source)], text=True, capture_output=True,
                                env=dict(os.environ, CETTA_PREPARED_PURE_REFERENCE=reference), timeout=30)
        observed = []
        for line in result.stdout.splitlines():
            try: value = json.loads(line)
            except ValueError: value = line
            if isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value):
                observed.append(value)
        assert result.returncode == 0 and not result.stderr, (dialect, result.stdout, result.stderr)
        assert observed == [hashlib.sha256(expected.encode()).hexdigest() for _, _, expected in cases], (dialect, reference, result.stdout)
print('PASS: 32 public envelope observations across HE/PeTTa and both evaluator routes')

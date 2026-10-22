#!/usr/bin/env python3
"""Prepared screen/rerank curve with bit-matched sequential NumPy controls.

Native direct and screened queries use one source, dtype and target schedule.
NumPy cumsum supplies the same sequential reference, not BLAS reassociation.
Its role here is observation validation; no NumPy speedup is claimed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--repetitions', type=int, default=7)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error('positive repetitions required')
    for variable in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
        os.environ[variable] = '1'
    import numpy as np

    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = dict(scope='packed numeric kernels, not evaluator/application parity',
        numpy=np.__version__, native_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),
        repetitions=args.repetitions, panels=[])
    sizes = (64, 2048, 16384, 65536) if args.full else (64, 2048, 16384)
    for rows in sizes:
        columns, k = 1024, min(64, rows)
        wire = args.output.parent / f'screen-scores-{rows}.ctn'
        started = time.perf_counter_ns()
        result = subprocess.run([str(args.native.resolve()), str(rows), str(columns), str(k),
            str(args.repetitions), str(wire)], text=True, capture_output=True, check=True, timeout=600)
        observation = json.loads(result.stdout)
        observation['whole_process_ns'] = time.perf_counter_ns() - started
        payload = wire.read_bytes()
        assert payload[:5] == b'CTN1\x02' and struct.unpack_from('<I', payload, 5)[0] == 1
        assert struct.unpack_from('<Q', payload, 9)[0] == rows
        actual = np.frombuffer(payload, dtype='<f8', offset=17)
        assert len(actual) == rows
        matrix = np.empty((rows, columns), dtype=np.float64)
        col = np.arange(columns, dtype=np.int64)[None, :]
        for start in range(0, rows, 1024):
            row = np.arange(start, min(rows, start + 1024), dtype=np.int64)[:, None]
            matrix[start:start + len(row)] = ((rows - row).astype(np.float64) / 65536. +
                (((row * 17 + col * 13) % 127) - 63).astype(np.float64) / 1024. +
                ((row + col) % 7).astype(np.float64) * float.fromhex('0x1p-45'))
        matrix *= 1. / columns
        np.cumsum(matrix, axis=1, out=matrix)
        expected = matrix[:, -1]
        np.testing.assert_array_equal(actual.view(np.uint64), expected.view(np.uint64))
        threshold = np.partition(expected, rows - k)[rows - k]
        above = np.flatnonzero(expected > threshold)
        tied = np.flatnonzero(expected == threshold)[:k - len(above)]
        selected = np.concatenate((above, tied))
        indices = selected[np.lexsort((selected, -expected[selected]))]
        assert observation['top_k'] == indices.tolist(), (rows, observation['top_k'], indices.tolist())
        observation['scores_bit_equal'] = True
        observation['top_k_equal'] = True
        observation['reference_matrix_bytes'] = matrix.nbytes
        report['panels'].append(observation)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(observation), flush=True)


if __name__ == '__main__':
    main()

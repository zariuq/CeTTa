#!/usr/bin/env python3
"""Resident query probes distinguish warm row/cache growth from ongoing retention."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from space_service_client import E, S, Tensor
from value_worker_client import Worker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    module = ROOT/'lib/petta/lib_memory_collection.metta'
    worker = Worker(args.worker.resolve(), module, 'memory:dispatch', profile=True)
    receipt = dict(status='failed', programme_complete=False,
                   worker_sha256=hashlib.sha256(args.worker.read_bytes()).hexdigest(),
                   module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(), checkpoints=[])
    try:
        vectors = [Tensor.vector([math.sin(i*.2), math.cos(i*.2)]) for i in range(1000)]
        for start in range(0, len(vectors), 25):
            rows = tuple(E('Memory', str(i), 1, 'fixture', vectors[i], 'generated', ())
                         for i in range(start, start+25))
            if worker.call(E('load', 'fixture', 2, rows)) != E('Loaded', 25):
                raise AssertionError('fixture load')
        for cycle in range(3):
            for i, vector in enumerate(vectors):
                answer = worker.call(E('query', 'fixture', vector, 1, S('All')))
                if len(answer) != 1 or answer[0][1][1] != str(i):
                    raise AssertionError('self-retrieval')
            receipt['checkpoints'].append(dict(cycle=cycle, **worker.last_metrics))
        for field in ('permanent_bytes', 'symbol_bytes', 'data_symbols'):
            if len({sample[field] for sample in receipt['checkpoints']}) != 1:
                raise AssertionError('retention after row warmup: '+field)
        filter_samples = []
        for i in range(3000):
            answer = worker.call(E('query', 'fixture', vectors[0], 1, E('Eq', 'missing', 'value-'+str(i))))
            if answer != ():
                raise AssertionError('empty filter')
            if i in (127, 999, 2999):
                filter_samples.append(dict(iteration=i, **worker.last_metrics))
        receipt['filters'] = filter_samples
        for field in ('permanent_bytes', 'symbol_bytes', 'data_symbols'):
            if len({sample[field] for sample in filter_samples}) != 1:
                raise AssertionError('retention after filter-cache warmup: '+field)
        receipt['status'] = 'passed'
    finally:
        worker.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2)+'\n')
    print('PASS: resident row and bounded-filter cache plateaus')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Answer-checked light timings; process startup is separate from warm calls."""
import argparse
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import time

from cetta import Callback, Engine


def measure(function, count, repeats):
    samples = []
    for _ in range(repeats):
        start = time.perf_counter_ns()
        for _ in range(count): function()
        samples.append((time.perf_counter_ns() - start) / count)
    return {'median_ns': statistics.median(samples), 'samples_ns': samples}


def measure_cases(cases, count, repeats):
    # Rotate the order so CPU warmup and competing work do not systematically
    # favor one interface. Imports and payload construction precede this loop.
    names = list(cases)
    samples = {name: [] for name in names}
    for function in cases.values():
        for _ in range(32): function()
    for repetition in range(repeats):
        offset = repetition % len(names)
        for name in names[offset:] + names[:offset]:
            function = cases[name]
            start = time.perf_counter_ns()
            for _ in range(count): function()
            samples[name].append((time.perf_counter_ns() - start) / count)
    return {name: {'median_ns': statistics.median(values), 'samples_ns': values}
            for name, values in samples.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--calls', type=int, default=1000)
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--lang', choices=('petta', 'he'), default='petta')
    args = parser.parse_args()
    if args.calls <= 0 or args.repeats <= 0: parser.error('counts must be positive')
    report = {'language': args.lang, 'calls': args.calls, 'repeats': args.repeats,
              'python': sys.version.split()[0], 'platform': platform.machine(),
              'load_average': os.getloadavg(), 'warm': {}, 'startup': {}}
    report['startup']['python_process'] = measure(
        lambda: subprocess.run([sys.executable, '-c', 'pass'], check=True, capture_output=True), 1, 3)
    report['startup']['python_import_engine'] = measure(lambda: subprocess.run(
        [sys.executable, '-c', 'from cetta import Engine; Engine().close()'],
        check=True, capture_output=True), 1, 3)
    with Engine(language=args.lang) as e:
        py = e.library('py')
        module = py['import']('math').one()
        sqrt = py.get(module, 'sqrt').one()
        py['bind!'](e.space.as_atom(), e.symbol('bench:sqrt'), sqrt)
        legacy = e.expression(e.symbol('math.sqrt'), 81)
        round_fn = e.pyobject(round)
        kw = py.kwargs(('ndigits', 2)).one()
        callback = e.register_callback('bench:callback', Callback(lambda x: x + 1, 1))
        del callback
        cases = {
            'direct_python': (lambda: math.sqrt(81), 9.0),
            'retained_callable': (lambda: e.call(sqrt, 81).one().to_python(), 9.0),
            'bound_name': (lambda: e.call('bench:sqrt', 81).one().to_python(), 9.0),
            'fresh_attribute': (lambda: e.call(py.get(module, 'sqrt').one(), 81).one().to_python(), 9.0),
            'fresh_import_attribute': (lambda: e.call(py.get(py['import']('math').one(), 'sqrt').one(), 81).one().to_python(), 9.0),
            'legacy_bridge': (lambda: e.call('py-call', legacy).one().to_python(), 9.0),
            'keywords': (lambda: e.call(round_fn, 1.234, kw).one().to_python(), 1.23),
            'callback': (lambda: e.call('bench:callback', 41).one().to_python(), 42),
        }
        for name, (function, expected) in cases.items():
            assert function() == expected, name
        report['warm'] = measure_cases({name: pair[0] for name, pair in cases.items()}, args.calls, args.repeats)
        wrapping = {}
        for size in (1, 1000, 1_000_000):
            payload = [None] * size
            def roundtrip(payload=payload):
                assert e.pyobject(payload).to_python() is payload
            wrapping[str(size)] = roundtrip
        report['wrapping'] = measure_cases(wrapping, args.calls, args.repeats)
        try:
            import numpy as np
        except ImportError:
            report['numerical'] = {'skipped': 'NumPy is not installed'}
        else:
            array = np.arange(1024, dtype=np.float64)
            value = e.pyobject(array)
            total = e.pyobject(np.sum)
            tensor = e.library('tensor')
            native = e.tensor(array)
            numeric = {
                'direct_numpy': lambda: np.sum(array),
                'python_object_numpy': lambda: e.call(total, value).one().to_python(),
                'native_tensor': lambda: tensor.sum(native).one().to_python(),
            }
            report['numerical'] = {}
            for name, function in numeric.items():
                assert function() == 523776.0, name
            report['numerical'] = measure_cases(numeric, args.calls, args.repeats)
    report['ratios'] = {
        'fresh_lookup_over_retained': report['warm']['fresh_import_attribute']['median_ns'] / report['warm']['retained_callable']['median_ns'],
        'legacy_over_retained': report['warm']['legacy_bridge']['median_ns'] / report['warm']['retained_callable']['median_ns'],
    }
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()

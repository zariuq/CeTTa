#!/usr/bin/env python3
"""Check the source-fuel boundary of the first-match case observer."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    binary = parser.parse_args().cetta.resolve()
    query = '!(case (once (match &self (missing 7) 11)) ((Empty absent) ($v present)))'
    env = {key: value for key, value in os.environ.items() if not key.startswith('CETTA_')}
    checks = 0
    for fuel in [None, *range(1, 11), 32, 1024]:
        observations = []
        for fusion in ['0', '1']:
            command = [str(binary), '--lang', 'petta']
            if fuel is not None:
                command += ['--fuel', str(fuel)]
            result = subprocess.run(command + ['-e', query], capture_output=True,
                                    timeout=30, env=dict(env, CETTA_PETTA_MATCH_EXISTENCE_FUSION=fusion))
            # The independent ordinary source path needs eight source visits.
            if fuel is not None and fuel < 8:
                assert result.returncode != 0 and not result.stdout, (fuel, fusion, result)
            else:
                assert result.returncode == 0 and result.stdout == b'absent\n' and not result.stderr, (
                    fuel, fusion, result)
            observations.append((result.returncode, result.stdout, result.stderr))
            checks += 1
        assert observations[0] == observations[1], (fuel, observations)
    print(f'PASS: {checks} first-match case source prefixes preserve incomplete and published outcomes')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Inject native allocation refusal without sharing a damaged test process."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess

ACTIONS = ('parse', 'eval', 'run', 'hyperpose', 'space', 'query', 'match', 'integer', 'bigint',
           'rational', 'string', 'large-string', 'expression', 'format', 'callback', 'register', 'buffer')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--mode', choices=('once', 'persistent', 'both'), default='both')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--actions', nargs='+', choices=ACTIONS, default=ACTIONS)
    parser.add_argument('--language', choices=('petta', 'he'))
    args = parser.parse_args()
    binary = args.consumer.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    modes = ('once', 'persistent') if args.mode == 'both' else (args.mode,)
    records = []
    for language in ((args.language,) if args.language else ('petta', 'he')):
        for action in args.actions:
            command = [str(binary), language, action]
            run = subprocess.run(command + [str(2**64 - 1), 'once'],
                                 capture_output=True, text=True, timeout=60)
            if run.returncode:
                raise AssertionError((language, action, 'positive', run.returncode, run.stdout, run.stderr))
            count = json.loads(run.stdout)['calls']
            for mode in modes:
                for index in range(count):
                    run = subprocess.run(command + [str(index), mode],
                                         capture_output=True, text=True, timeout=60)
                    if run.returncode:
                        name = f'{language}-{action}-{mode}-{index}'
                        (args.output / (name + '.log')).write_text(run.stdout + run.stderr)
                        raise AssertionError((name, run.returncode, run.stdout, run.stderr))
                    observation = json.loads(run.stdout)
                    records.append(dict(language=language, action=action, index=index, mode=mode,
                                        **observation))
                print(f'EmbeddingMemory {language} {action} {mode}: {count} refusals pass', flush=True)
    receipt = {'consumer_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'checks': records}
    (args.output / 'results.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'EmbeddingMemory total: {len(records)} refusals pass')


if __name__ == '__main__':
    main()

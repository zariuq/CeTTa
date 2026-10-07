#!/usr/bin/env python3
"""A working native control and a specific assertion must reject its defect."""
import argparse
from pathlib import Path
import signal
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('control', type=Path)
parser.add_argument('mutant', type=Path)
parser.add_argument('assertion')
args = parser.parse_args()
control = subprocess.run([str(args.control.resolve())], capture_output=True,
                         text=True, timeout=180)
assert control.returncode == 0 and 'PASS:' in control.stdout, control
mutant = subprocess.run([str(args.mutant.resolve())], capture_output=True,
                        text=True, timeout=180)
assert mutant.returncode == -signal.SIGABRT and args.assertion in mutant.stderr, (
    mutant.returncode, mutant.stdout, mutant.stderr)
print(f'PASS: native control accepted; mutation rejected by {args.assertion}')

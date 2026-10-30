#!/usr/bin/env python3
"""Run a command and print, after its own output, the peak resident memory
of the processes it ran, in KiB.

usage: peak_rss.py COMMAND [ARG...]"""
import resource
import subprocess
import sys

done = subprocess.run(sys.argv[1:])
print(f"peak-rss-kib {resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}")
sys.exit(done.returncode)

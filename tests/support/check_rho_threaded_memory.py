#!/usr/bin/env python3
"""Peak RSS of threaded rho loops through the command line.

The duplicator loop x!(D(x)) | D(x) (strict core) and a persistent receive
that answers its own sends (rhometta) run 10^4 and then 10^6 COMMs on 2 and
4 workers.  Each run's tasks hold only what they need, so the longer run's
peak RSS is the shorter one's, within a slack far below what a COMM's
working memory per COMM would add (a few hundred bytes to kilobytes each).
"""

from __future__ import annotations

import os
import subprocess
import sys

DUP = "(rho:recv (rho:quote rho:nil) $y (rho:par (rho:drop $y) (rho:send (rho:quote rho:nil) (rho:drop $y))))"
LOOPS = {
    "duplicator loop": (["--lang", "rhocalc", "--syntax", "mrho"],
                        f"(rho:par {DUP} (rho:send (rho:quote rho:nil) {DUP}))"),
    "persistent receive loop": (["--lang", "rhocalc", "--profile", "rhometta", "--syntax", "mrho"],
                                "(rho:par (rho:recv-persistent $c $x (rho:send $c (rho:drop $x))) (rho:send $c rho:nil))"),
}
SHORT, LONG = 10_000, 1_000_000
SLACK_KIB = 8 * 1024


# A child's peak RSS as wait4 reports it counts the memory of the process
# that forked it (this interpreter, some 12 MB), so a run is measured through
# GNU time when it is there, which forks it from a small process.
GNU_TIME = "/usr/bin/time" if os.access("/usr/bin/time", os.X_OK) else None

# A binary built with AddressSanitizer keeps what it frees in a quarantine
# that grows to hundreds of megabytes, which the peak RSS would measure in
# place of the run; the runs are measured with it off.  A binary built
# without it ignores the setting.
RUN_ENV = dict(os.environ)
RUN_ENV["ASAN_OPTIONS"] = ":".join(
    part for part in (RUN_ENV.get("ASAN_OPTIONS", ""), "quarantine_size_mb=0") if part)


def peak_rss_kib(binary: str, workers: int, comms: int, args: list[str], program: str) -> tuple[int, int]:
    command = [binary, "--num-threads", str(workers), "--rho-reduction-limit", str(comms), *args, "-e", program]
    if GNU_TIME:
        report = subprocess.run([GNU_TIME, "-f", "%x %M", *command], stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, text=True, env=RUN_ENV)
        status, rss = report.stderr.strip().splitlines()[-1].split()
        return int(status), int(rss)
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=RUN_ENV)
    _, status, usage = os.wait4(process.pid, 0)
    return os.waitstatus_to_exitcode(status), usage.ru_maxrss


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    failed = False
    for label, (args, program) in LOOPS.items():
        for workers in (2, 4):
            short_status, short_rss = peak_rss_kib(binary, workers, SHORT, args, program)
            long_status, long_rss = peak_rss_kib(binary, workers, LONG, args, program)
            # 3: the reduction limit ran out, as these loops never end.
            ok = short_status == 3 and long_status == 3 and long_rss <= short_rss + SLACK_KIB
            print(f"{'PASS' if ok else 'FAIL'}: {label} on {workers} workers: peak RSS "
                  f"{short_rss} KiB after {SHORT} COMMs, {long_rss} KiB after {LONG}")
            failed = failed or not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

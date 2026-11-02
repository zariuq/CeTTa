#!/usr/bin/env python3
"""Cyclic temporary spaces stay bounded; externally rooted cycles stay usable."""
import os
import sys

from check_escaped_space_reclamation import measured

SELF = """(= (churn $n) (if (== $n 0) done
  (let $s (new-space)
    (let $_ (add-atom $s (self $s))
      (let $_ (add-atom $s (payload $n))
        {keep}(churn (- $n 1)){close})))))
!(churn {n})
"""

MUTUAL = """(= (churn $n) (if (== $n 0) done
  (let $a (new-space)
    (let $b (new-space)
      (let $_ (add-atom $a (next $b))
        (let $_ (add-atom $b (next $a))
          {keep}(churn (- $n 1)){close}))))))
!(churn {n})
"""


def main():
    cetta = os.path.abspath(sys.argv[1])
    sizes = (8000, 64000, 640000)
    for name, body in (("self", SELF), ("mutual", MUTUAL)):
        peaks = [measured(cetta, body.format(n=n, keep="", close=""), ["done"])
                 for n in sizes]
        if max(peaks) - min(peaks) > 8192:
            raise SystemExit(f"FAIL: {name} cycles retain memory: {list(zip(sizes, peaks))}")
        print(f"PASS: {name} cycles stay bounded {list(zip(sizes, peaks))} KiB")
    kept = SELF.format(n=8000,
        keep="(let $_ (if (== (% $n 1000) 0) (add-atom &self (kept $s)) ()) ",
        close=")") + """!(collapse (match &self (kept $s)
  (match $s (self $t) (if (== $s $t) same different))))
!(collapse (match &self (kept $s) (match $s (payload 8000) found)))
"""
    measured(cetta, kept, ["done", "(same"] + ["same"] * 6 +
             ["same)", "(found)"])
    print("PASS: retained cyclic spaces preserve identity and payloads")


if __name__ == "__main__":
    main()

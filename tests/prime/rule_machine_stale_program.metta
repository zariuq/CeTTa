; A structurally plausible program from a different GSLT revision is stale.

!(compile:rule-program-run
  (rule-program-v1
    HilbertBFCProgramV1 stale-gslt-identity bad-r0 proof-occurrence-bag
    (compile-delta 1 1 0)
    (rule-program-empty HilbertBFCProgramV1 stale-gslt-identity))
  1 10 10 target)

; An empty RuleMachineProgram program cannot satisfy the bounded fragment's required
; exactly-one inverse-modus-ponens invariant.

!(compile:rule-program-run
  (rule-program-v1
    HilbertBFCProgramV1 bad-r0 proof-occurrence-bag
    (compile-delta 0 0 0)
    (rule-program-empty HilbertBFCProgramV1))
  1 10 10 target)

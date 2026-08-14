; Authored compressed-trace policy shared by state-plan and trace-program
; compilation.  The generic compilers consume this closed policy code; neither
; generated C realization chooses a guest policy.

(gslt-presentation-v1 MetamathProofTracePolicyV1
  (signature
    (operator source-proof-trace-save-placement-v1 1))
  (equations)
  (rewrites
    (rule mm-proof-trace-save-immediately-after-use-v1
      (head
        (source-proof-trace-save-placement-v1
          state-proof-save-immediately-after-use-v1))
      (body))
  ))

; Generated from the Metamath source operation algebra.
; This is the explicit composition into generic indexed-checker effects.

(gslt-presentation-v1 MetamathSourceCheckerV1
  (signature)
  (equations)
  (rewrites
    (rule mm-checker-open-scope
      (head (source-checker-effect mm-open-scope checker-open-scope))
      (body))
    (rule mm-checker-close-scope
      (head (source-checker-effect mm-close-scope checker-close-scope))
      (body))
    (rule mm-checker-declare-constants
      (head (source-checker-effect mm-declare-constants checker-declare-constants))
      (body))
    (rule mm-checker-declare-variables
      (head (source-checker-effect mm-declare-variables checker-declare-variables))
      (body))
    (rule mm-checker-declare-distinct
      (head (source-checker-effect mm-declare-disjoint checker-declare-distinct))
      (body))
    (rule mm-checker-declare-floating
      (head (source-checker-effect mm-declare-floating checker-declare-floating))
      (body))
    (rule mm-checker-declare-essential
      (head (source-checker-effect mm-declare-essential checker-declare-essential))
      (body))
    (rule mm-checker-declare-axiom
      (head (source-checker-effect mm-declare-axiom checker-declare-rule))
      (body))
    (rule mm-checker-theorem-normal
      (head (source-checker-effect mm-check-theorem-normal checker-check-normal))
      (body))
    (rule mm-checker-theorem-compressed
      (head (source-checker-effect mm-check-theorem-compressed checker-check-compressed))
      (body))
    (rule mm-checker-resolve-include
      (head (source-checker-effect mm-resolve-include checker-resolve-source))
      (body))
    (rule mm-checker-complete-block
      (head (source-checker-effect mm-complete-block checker-complete-scope))
      (body))
    (rule mm-checker-policy-inner-constants
      (head (source-checker-policy checker-allow-inner-constants policy-false))
      (body))
    (rule mm-checker-policy-duplicate-floating
      (head (source-checker-policy checker-allow-duplicate-floating policy-false))
      (body))
    (rule mm-checker-policy-toplevel-essential
      (head (source-checker-policy checker-allow-toplevel-essential policy-true))
      (body))
    (rule mm-checker-policy-unknown-steps
      (head (source-checker-policy checker-reject-unknown-steps policy-false))
      (body))
    (rule mm-checker-policy-completed-sources
      (head (source-checker-policy checker-skip-completed-sources policy-true))
      (body))
    (rule mm-checker-policy-active-source-cycles
      (head (source-checker-policy checker-reject-active-source-cycles policy-true))
      (body))
  ))

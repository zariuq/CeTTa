; Language-neutral effects for incrementally constructing an indexed
; inference-checker presentation from ordered source occurrences.

(gslt-presentation-v1 IndexedCheckerFoldCoreV1
  (signature
    (operator indexed-checker-effect 1)
    (operator indexed-checker-policy-key 1)
    (operator indexed-checker-policy-value 1)
    (operator source-checker-effect 2)
    (operator source-checker-policy 2)
  )
  (equations)
  (rewrites
    (rule indexed-checker-effect-open-scope
      (head (indexed-checker-effect checker-open-scope))
      (body))
    (rule indexed-checker-effect-close-scope
      (head (indexed-checker-effect checker-close-scope))
      (body))
    (rule indexed-checker-effect-declare-constants
      (head (indexed-checker-effect checker-declare-constants))
      (body))
    (rule indexed-checker-effect-declare-variables
      (head (indexed-checker-effect checker-declare-variables))
      (body))
    (rule indexed-checker-effect-declare-distinct
      (head (indexed-checker-effect checker-declare-distinct))
      (body))
    (rule indexed-checker-effect-declare-floating
      (head (indexed-checker-effect checker-declare-floating))
      (body))
    (rule indexed-checker-effect-declare-essential
      (head (indexed-checker-effect checker-declare-essential))
      (body))
    (rule indexed-checker-effect-declare-rule
      (head (indexed-checker-effect checker-declare-rule))
      (body))
    (rule indexed-checker-effect-check-normal
      (head (indexed-checker-effect checker-check-normal))
      (body))
    (rule indexed-checker-effect-check-compressed
      (head (indexed-checker-effect checker-check-compressed))
      (body))
    (rule indexed-checker-effect-resolve-source
      (head (indexed-checker-effect checker-resolve-source))
      (body))
    (rule indexed-checker-effect-complete-scope
      (head (indexed-checker-effect checker-complete-scope))
      (body))

    (rule indexed-checker-policy-key-allow-inner-constants
      (head
        (indexed-checker-policy-key checker-allow-inner-constants))
      (body))
    (rule indexed-checker-policy-key-allow-duplicate-floating
      (head
        (indexed-checker-policy-key checker-allow-duplicate-floating))
      (body))
    (rule indexed-checker-policy-key-allow-toplevel-essential
      (head
        (indexed-checker-policy-key checker-allow-toplevel-essential))
      (body))
    (rule indexed-checker-policy-key-reject-unknown-steps
      (head
        (indexed-checker-policy-key checker-reject-unknown-steps))
      (body))
    (rule indexed-checker-policy-key-skip-completed-sources
      (head
        (indexed-checker-policy-key checker-skip-completed-sources))
      (body))
    (rule indexed-checker-policy-key-reject-active-source-cycles
      (head
        (indexed-checker-policy-key
          checker-reject-active-source-cycles))
      (body))
    (rule indexed-checker-policy-value-false
      (head (indexed-checker-policy-value policy-false))
      (body))
    (rule indexed-checker-policy-value-true
      (head (indexed-checker-policy-value policy-true))
      (body))
  ))

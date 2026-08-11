; A structurally small finite-Horn program for the native OSLF program ABI.
; Its inventory is deliberately unrelated to proof-frame layouts: six heads,
; no apartness relation, ground scalar facts, and one recursive rule.

(gslt-presentation-v1 OSLFNativeProgramCanaryV1
  (signature
    (operator canary-edge-v1 2)
    (operator canary-mark-v1 1)
    (operator canary-path-v1 2)
    (operator canary-wide-v1 1)
    (operator canary-list-cons-v1 2)
    (operator canary-list-proof-v1 1)
    (operator canary-tail-choice-v1 1)
    (operator canary-tail-middle-v1 1)
    (operator canary-tail-leaf-v1 1)
  )
  (equations)
  (rewrites
    (rule canary-edge-seed-v1
      (head (canary-edge-v1 "root-λ" 7))
      (body))
    (rule canary-edge-dead-v1
      (head (canary-edge-v1 "root-λ" dead-end))
      (body))
    (rule canary-edge-leaf-v1
      (head (canary-edge-v1 7 "leaf"))
      (body))
    (rule canary-mark-seed-v1
      (head (canary-mark-v1 root))
      (body))
    (rule canary-wide-integer-v1
      (head (canary-wide-v1 123456789012345678901234567890))
      (body))
    (rule canary-path-from-edge-v1
      (head (canary-path-v1 ?left ?right))
      (body (canary-edge-v1 ?left ?right)))
    (rule canary-path-transitive-v1
      (head (canary-path-v1 ?left ?right))
      (body
        (canary-edge-v1 ?left ?middle)
        (canary-path-v1 ?middle ?right)))
    (rule canary-list-proof-nil-v1
      (head (canary-list-proof-v1 canary-list-nil-v1))
      (body))
    (rule canary-list-proof-cons-v1
      (head
        (canary-list-proof-v1
          (canary-list-cons-v1 ?item ?tail)))
      (body (canary-list-proof-v1 ?tail)))
    (rule canary-tail-choice-dead-v1
      (head (canary-tail-choice-v1 go))
      (body (canary-tail-middle-v1 dead)))
    (rule canary-tail-choice-live-v1
      (head (canary-tail-choice-v1 go))
      (body (canary-tail-middle-v1 live)))
    (rule canary-tail-middle-v1
      (head (canary-tail-middle-v1 ?value))
      (body (canary-tail-leaf-v1 ?value)))
    (rule canary-tail-leaf-live-v1
      (head (canary-tail-leaf-v1 live))
      (body))
  ))

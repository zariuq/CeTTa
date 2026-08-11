; A structurally open finite-Horn program.  Its external relation is admitted
; as finite extensional input, while the generic VM owns only rule execution.

(gslt-presentation-v1 OSLFNativeOpenProgramV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
    (operator canary-open-edge-v1 2)
    (operator canary-open-path-v1 2)
    (operator canary-open-source-v1 1)
    (operator canary-open-two-v1 1)
  )
  (equations)
  (rewrites
    (rule canary-open-edge-interface-v1
      (head
        (oslf-external-relation-decl-v1 canary-open-edge-v1 2))
      (body))
    (rule canary-open-path-from-edge-v1
      (head (canary-open-path-v1 ?x ?y))
      (body (canary-open-edge-v1 ?x ?y)))
    (rule canary-open-source-from-edge-v1
      (head (canary-open-source-v1 ?x))
      (body (canary-open-edge-v1 ?x ?y)))
    (rule canary-open-two-from-edge-v1
      (head (canary-open-two-v1 ?x))
      (body
        (canary-open-edge-v1 ?x ?y)
        (canary-open-edge-v1 ?x ?z)))
  ))

; Independent capability extension used only to exercise the open grounding
; portal.  It is not part of the default MeTTa Zero generated pack.
(gslt-presentation-v1 MettaZeroGroundCapabilityCanaryV1
  (signature
    (operator q-str 1)
    (operator q-sym 1)
    (operator zero-ground-apply 3))
  (equations)
  (rewrites
    (rule metta-zero-ground-capability-canary
      (head
        (zero-ground-apply
          (q-sym (q-str "native"))
          zero-ground-proof
          (q-sym (q-str "grounded"))))
      (body))
  ))

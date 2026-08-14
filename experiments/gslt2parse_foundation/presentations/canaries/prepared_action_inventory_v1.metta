; An additive structural canary for the generated source-kind classifier.
; Five opaque source kinds map into the closed generic stack-action algebra;
; neither the compiler nor the runtime may assume a four-role inventory.

(gslt-presentation-v1 PreparedActionInventoryCanaryV1
  (signature
    (operator state-proof-action-v1 3))
  (equations)
  (rewrites
    (rule prepared-action-canary-00
      (head
        (state-proof-action-v1
          guest-c-machine guest-c-binder-kind stack-push-declared-v1))
      (body))
    (rule prepared-action-canary-01
      (head
        (state-proof-action-v1
          guest-c-machine guest-c-matching-kind stack-push-declared-v1))
      (body))
    (rule prepared-action-canary-02
      (head
        (state-proof-action-v1
          guest-c-machine guest-c-introduction-kind stack-apply-frame-v1))
      (body))
    (rule prepared-action-canary-03
      (head
        (state-proof-action-v1
          guest-c-machine guest-c-elimination-kind stack-apply-frame-v1))
      (body))
    (rule prepared-action-canary-04
      (head
        (state-proof-action-v1
          guest-c-machine guest-c-freshness-kind stack-apply-frame-v1))
      (body))))

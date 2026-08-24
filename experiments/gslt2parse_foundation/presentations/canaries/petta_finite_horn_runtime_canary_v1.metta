(gslt-presentation-v1 PeTTaFiniteHornRuntimeCanaryV1
  (signature
    (operator CanaryParentV1 2)
    (operator CanaryAncestorV1 2)
    (operator CanaryLoopV1 1))
  (equations)
  (rewrites
    (rule canary-parent-alice-bob-v1
      (head (CanaryParentV1 CanaryAliceV1 CanaryBobV1))
      (body))
    (rule canary-parent-bob-cara-v1
      (head (CanaryParentV1 CanaryBobV1 CanaryCaraV1))
      (body))
    (rule canary-parent-bob-dara-v1
      (head (CanaryParentV1 CanaryBobV1 CanaryDaraV1))
      (body))
    (rule canary-ancestor-base-v1
      (head (CanaryAncestorV1 ?first ?last))
      (body
        (CanaryParentV1 ?first ?last)))
    (rule canary-ancestor-step-v1
      (head (CanaryAncestorV1 ?first ?last))
      (body
        (CanaryParentV1 ?first ?middle)
        (CanaryAncestorV1 ?middle ?last)))
    (rule canary-loop-self-v1
      (head (CanaryLoopV1 ?value))
      (body
        (CanaryLoopV1 ?value)))))

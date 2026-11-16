(gslt-presentation-v1 BeforeEmptyPrefixV1
  (signature
    (operator MissingV1 2)
    (operator UsesLoopV1 2)
    (operator BlockV1 1)
    (operator gslt:BlockV1 1)
    (operator block-again-v1 1)
    (operator oslf-external-relation-decl-v1 2)
    (operator metta-equation 2))
  (equations)
  (rewrites
    (rule block-capability-v1
      (head (oslf-external-relation-decl-v1 BlockV1 1))
      (body))
    (rule block-forward-v1
      (head (metta-equation (gslt:BlockV1 (BlockV1 ?x)) (block-again-v1 ?x)))
      (body))
    (rule block-back-v1
      (head (metta-equation (block-again-v1 ?x) (gslt:BlockV1 (BlockV1 ?x))))
      (body))
    (rule before-empty-v1
      (head (UsesLoopV1 ?input ?output))
      (body (BlockV1 ?input) (MissingV1 ?input ?output)))))

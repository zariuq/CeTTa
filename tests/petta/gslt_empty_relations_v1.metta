(gslt-presentation-v1 EmptyRelationsV1
  (signature
    (operator MissingV1 2)
    (operator UsesMissingV1 2)
    (operator NoFactsV1 0)
    (operator NoFactsV1 1)
    (operator UsesNullaryV1 1)
    (operator PresentV1 1))
  (equations)
  (rewrites
    (rule through-empty-v1
      (head (UsesMissingV1 ?input ?output))
      (body (MissingV1 ?input ?output)))
    (rule independent-present-v1
      (head (PresentV1 present))
      (body))
    (rule through-nullary-v1
      (head (UsesNullaryV1 ?input))
      (body (NoFactsV1)))
    (rule other-arity-present-v1
      (head (NoFactsV1 present))
      (body))))

(gslt-presentation-v1 ProviderCallCanaryV1
  (signature
    (operator provider-entry 2)
    (operator provider-external 2))
  (equations)
  (rewrites
    (rule provider-entry-through-external
      (head (provider-entry ?program ?answer))
      (body (provider-external ?program ?answer)))
  ))

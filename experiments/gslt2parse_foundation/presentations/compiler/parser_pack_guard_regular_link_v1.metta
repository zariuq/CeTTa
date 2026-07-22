(gslt-presentation-v1 ParserPackGuardRegularLinkV1
  (signature)
  (equations)
  (rewrites
    (rule compile-span-definition-positive-guard
      (head
        (compile-span-definition
          ?tag (rx-definition ?tag ?regex)))
      (body
        (compile-pack-positive-guard
          ?owner
          (pp-positive-guard-v1
            ?state ?tag ?body ?production))
        (compile-span-grammar ?body ?regex)))
  ))

(gslt-presentation-v1 ParserPackGuardActionLinkV1
  (signature
    (operator compile-guard-extended-action-program 5)
    (operator compile-positive-guard-action-program 5)
  )
  (equations)
  (rewrites
    (rule compile-positive-guard-action-program-v1
      (head
        (compile-positive-guard-action-program
          ?owner ?label ?arity ?action ?code))
      (body
        (compile-pack-positive-guard
          ?owner
          (pp-positive-guard-v1
            ?state
            ?tag
            ?body
            (pp-production ?label ?state ?items ?action)))
        (pack-items-length ?items ?arity)
        (lower-pack-action ?action ?code)))

    (rule compile-guard-extended-action-program-base
      (head
        (compile-guard-extended-action-program
          ?owner ?label ?arity ?action ?code))
      (body
        (compile-pack-action-program
          ?owner ?label ?arity ?action ?code)))
    (rule compile-guard-extended-action-program-extension
      (head
        (compile-guard-extended-action-program
          ?owner ?label ?arity ?action ?code))
      (body
        (compile-positive-guard-action-program
          ?owner ?label ?arity ?action ?code)))
  ))

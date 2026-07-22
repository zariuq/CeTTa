(gslt-presentation-v1 ParserPackGuardCompilerV1
  (signature
    (operator collect-sir-positive-guard 3)
    (operator compile-pack-positive-guard 2)
    (operator pp-positive-guard-tag 1)
    (operator pp-positive-guard-v1 4)
    (operator pp-span-terminal 1)
  )
  (equations)
  (rewrites
    (rule compile-pack-positive-guard-definition
      (head
        (compile-pack-positive-guard ?name ?guard))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (collect-sir-positive-guard
          (pp-def ?name) ?grammar ?guard)))

    (rule collect-sir-positive-guard-here
      (head
        (collect-sir-positive-guard
          ?state
          (sir-peek ?body)
          (pp-positive-guard-v1
            ?state
            (pp-positive-guard-tag ?state)
            ?body
            (pp-production
              (pp-label ?state peek)
              ?state
              (pp-items-cons
                (pp-terminal
                  (pp-span-terminal
                    (pp-positive-guard-tag ?state)))
                pp-items-nil)
              (pa-slot q-zero)))))
      (body))
    (rule collect-sir-positive-guard-descend
      (head
        (collect-sir-positive-guard
          ?state (sir-peek ?body) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state body) ?body ?guard)))

    (rule collect-sir-alt-left
      (head
        (collect-sir-positive-guard
          ?state (sir-alt ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state left) ?left ?guard)))
    (rule collect-sir-alt-right
      (head
        (collect-sir-positive-guard
          ?state (sir-alt ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state right) ?right ?guard)))

    (rule collect-sir-seq-left
      (head
        (collect-sir-positive-guard
          ?state (sir-seq ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state left) ?left ?guard)))
    (rule collect-sir-seq-right
      (head
        (collect-sir-positive-guard
          ?state (sir-seq ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state right) ?right ?guard)))

    (rule collect-sir-left-left
      (head
        (collect-sir-positive-guard
          ?state (sir-left ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state left) ?left ?guard)))
    (rule collect-sir-left-right
      (head
        (collect-sir-positive-guard
          ?state (sir-left ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state right) ?right ?guard)))

    (rule collect-sir-right-left
      (head
        (collect-sir-positive-guard
          ?state (sir-right ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state left) ?left ?guard)))
    (rule collect-sir-right-right
      (head
        (collect-sir-positive-guard
          ?state (sir-right ?left ?right) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state right) ?right ?guard)))

    (rule collect-sir-node-body
      (head
        (collect-sir-positive-guard
          ?state (sir-node ?label ?body) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state body) ?body ?guard)))
    (rule collect-sir-star-body
      (head
        (collect-sir-positive-guard
          ?state (sir-star ?body) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state body) ?body ?guard)))
    (rule collect-sir-plus-body
      (head
        (collect-sir-positive-guard
          ?state (sir-plus ?body) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state body) ?body ?guard)))
    (rule collect-sir-opt-body
      (head
        (collect-sir-positive-guard
          ?state (sir-opt ?body) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state body) ?body ?guard)))

    (rule collect-sir-until-end
      (head
        (collect-sir-positive-guard
          ?state (sir-until ?end ?item) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state end) ?end ?guard)))
    (rule collect-sir-until-item
      (head
        (collect-sir-positive-guard
          ?state (sir-until ?end ?item) ?guard))
      (body
        (collect-sir-positive-guard
          (pp-sub ?state item) ?item ?guard)))
  ))

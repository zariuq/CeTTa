; Lower the regular prefix of definitions whose trailing positive guard is
; syntactically exposed as (left PREFIX (peek GUARD)).  The emitted guard
; state and body keep the over-approximating prefix NFA inseparable from the
; trailing-context obligation that makes its accepted spans exact.

(gslt-presentation-v1 GuardedRegularSpanCompilerV1
  (signature
    (operator compile-guarded-span-definition 4)
    (operator compile-guarded-span-nfa 4)
    (operator project-sir-trailing-positive-guard 5)
  )
  (equations)
  (rewrites
    (rule compile-guarded-span-definition
      (head
        (compile-guarded-span-definition
          ?name
          ?guard-state
          ?guard
          (rx-definition ?name ?regex)))
      (body
        (compile-definition
          ?name
          (sir-definition ?name ?grammar))
        (project-sir-trailing-positive-guard
          (pp-def ?name)
          ?grammar
          ?guard-state
          ?guard
          ?prefix)
        (compile-span-grammar ?prefix ?regex)))

    (rule project-trailing-guard-left-here
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-left ?prefix (sir-peek ?guard))
          (pp-sub ?state right)
          ?guard
          ?prefix))
      (body))
    (rule project-trailing-guard-seq-here
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-seq ?prefix (sir-peek ?guard))
          (pp-sub ?state right)
          ?guard
          ?prefix))
      (body))
    (rule project-trailing-guard-right-here
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-right ?prefix (sir-peek ?guard))
          (pp-sub ?state right)
          ?guard
          ?prefix))
      (body))

    (rule project-trailing-guard-node-body
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-node ?label ?body)
          ?guard-state
          ?guard
          ?prefix))
      (body
        (project-sir-trailing-positive-guard
          (pp-sub ?state body)
          ?body
          ?guard-state
          ?guard
          ?prefix)))
    (rule project-trailing-guard-seq-right
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-seq ?left ?right)
          ?guard-state
          ?guard
          (sir-seq ?left ?prefix)))
      (body
        (project-sir-trailing-positive-guard
          (pp-sub ?state right)
          ?right
          ?guard-state
          ?guard
          ?prefix)))
    (rule project-trailing-guard-left-right
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-left ?left ?right)
          ?guard-state
          ?guard
          (sir-seq ?left ?prefix)))
      (body
        (project-sir-trailing-positive-guard
          (pp-sub ?state right)
          ?right
          ?guard-state
          ?guard
          ?prefix)))
    (rule project-trailing-guard-right-right
      (head
        (project-sir-trailing-positive-guard
          ?state
          (sir-right ?left ?right)
          ?guard-state
          ?guard
          (sir-seq ?left ?prefix)))
      (body
        (project-sir-trailing-positive-guard
          (pp-sub ?state right)
          ?right
          ?guard-state
          ?guard
          ?prefix)))

    (rule compile-guarded-span-nfa-start
      (head
        (compile-guarded-span-nfa
          ?tag ?guard-state ?guard
          (nfa-start (rn-in (rn-root ?tag)))))
      (body
        (compile-guarded-span-definition
          ?tag ?guard-state ?guard
          (rx-definition ?tag ?regex))))
    (rule compile-guarded-span-nfa-accept
      (head
        (compile-guarded-span-nfa
          ?tag ?guard-state ?guard
          (nfa-accept (rn-out (rn-root ?tag)) ?tag)))
      (body
        (compile-guarded-span-definition
          ?tag ?guard-state ?guard
          (rx-definition ?tag ?regex))))
    (rule compile-guarded-span-nfa-edge
      (head
        (compile-guarded-span-nfa
          ?tag ?guard-state ?guard ?edge))
      (body
        (compile-guarded-span-definition
          ?tag ?guard-state ?guard
          (rx-definition ?tag ?regex))
        (span-nfa-edge
          ?tag ?regex (rn-root ?tag) ?edge)))
  ))

; A familiar spelling must not override its declared, unavailable identity.
(gslt-support-transform-language-v1
  (name future-profile)
  (profile gslt)
  (carrier support)
  (work-shell work 3 2 0 1)
  (scheduler least-mork-compact-expression-key-v1)
  (unsupported leave-inert)
  (transition consume-selected snapshot-includes-selected
    relational-product-may-reuse-support stage-all-matches-per-sink
    finalize-sinks-left-to-right)
  (input-compat all support.snapshot-match.v1)
  (input-explicit from (source BTM 1 future.snapshot-match.v1))
  (output-compat emit support.add.v1)
  (output-explicit do (sink put 1 support.add.v1))
  (fuel exact-upper-bound)
  (completion no-supported-work)
  (observation support))

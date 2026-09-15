; Quoting, UTF-8, and empty declaration collections are descriptor data.
(gslt-support-transform-language-v1
  (name "日本語 \" \\ ?")
  (profile "profile;quoted")
  (carrier support)
  (work-shell work 3 2 0 1)
  (scheduler least-mork-compact-expression-key-v1)
  (unsupported leave-inert)
  (transition consume-selected snapshot-includes-selected
    relational-product-may-reuse-support stage-all-matches-per-sink
    finalize-sinks-left-to-right)
  (input-compat all support.snapshot-match.v1)
  (input-explicit from)
  (output-compat emit support.add.v1)
  (output-explicit do)
  (fuel exact-upper-bound)
  (completion no-supported-work)
  (observation support))

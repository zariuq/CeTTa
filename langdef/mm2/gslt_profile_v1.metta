; Strict, open-world MM2 support-transform profile.
;
; This presentation selects one finite-set work-queue GSLT.  Its C
; realizations are generic over every symbol below: the authored profile owns
; the vocabulary and policies, while native C and Rust/PathMap via C ABI own
; only structural terms, matching, finite support, exact resource bounds, and
; diagnostics.
(gslt-support-transform-language-v1
  (name mm2)
  (profile gslt)
  (carrier support)
  (work-shell exec 3 0 1 2)
  (scheduler least-mork-compact-expression-key-v1)
  (unsupported leave-inert)
  (transition
    consume-selected
    snapshot-includes-selected
    relational-product-may-reuse-support
    stage-all-matches-per-sink
    finalize-sinks-left-to-right)
  (input-compat "," support.snapshot-match.v1)
  (input-explicit I
    (source BTM 1 support.snapshot-match.v1)
    (source == 2 support.equal.v1)
    (source != 2 support.not-equal.v1))
  (output-compat "," support.add.v1)
  (output-explicit O
    (sink + 1 support.add.v1)
    (sink - 1 support.remove.v1)
    (sink head 2 support.head.v1)
    (sink tail 2 support.tail.v1)
    (sink count 3 support.group-cardinality.v1)
    (sink pure 3 support.evaluate-project.mm2-pure-f64.v1))
  (fuel exact-upper-bound)
  (completion no-supported-work)
  (observation support))

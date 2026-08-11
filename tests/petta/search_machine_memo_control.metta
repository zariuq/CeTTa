!(import_prolog_function
  (superpose
    (memoize
     config-memoize
     get-memoize-config
     clear-memoize
     invalidate-memoize
     is-memoized
     get-memoize-stats
     clear-memoize-stats)))

; Declaration order is not an execution-order restriction: the complete
; document has already declared the relation head when this request runs.
!(memoize sq)
(= (sq $x) (* $x $x))

!(sq 9)
!(sq 9)
!(is-memoized sq)
!(is-memoized sq 1)
!(is-memoized sq 2)
!(get-memoize-stats)
!(clear-memoize-stats)
!(get-memoize-stats)

; Invalidation discards retained answers without disabling the relation.
!(invalidate-memoize sq)
!(sq 9)
!(sq 9)
!(get-memoize-stats)

; Clearing discards all entries and resets the counters together.
!(clear-memoize)
!(get-memoize-stats)
!(sq 9)
!(get-memoize-stats)
!(get-memoize-config)

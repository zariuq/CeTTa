; Regressions for body-visible substitution being re-applied during final
; materialization. Once a let/chain body has been structurally rewritten with
; the visible env, that same env must not be replayed over the emitted result.

!(assertEqual
  (let $x (quote (let $x 2 $x))
    (quote (tag $x)))
  (quote (tag (quote (let $x 2 $x)))))

!(assertEqual
  (let $groups (quote (size-atom $groups))
    (quote (extracted-key-count $groups)))
  (quote (extracted-key-count (quote (size-atom $groups)))))

!(assertEqual
  (chain (quote (let $x 2 $x)) $x
    (quote (tag $x)))
  (quote (tag (quote (let $x 2 $x)))))

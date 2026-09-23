; Exact closure summaries are transitive across private Need references.
; A matcher inside a delayed term may refine a free variable, and wrapping
; that term in another Need cell must preserve the same lexical binding.

(= (prime-need:capture-match A) ok)

; Direct capture control.
!(let $promise (delay (prime-need:capture-match $x))
   (let $_ (force $promise) $x))

; Positive discriminator: the outer cell contains only an inner private
; reference syntactically, but it captures the inner cell's exact `$x`.
!(let $inner (delay (prime-need:capture-match $x))
   (let $outer (delay (force $inner))
     (let $_ (force $outer) $x)))

; Negative discriminator: a rule-local `$x` is freshened and must not bind an
; unrelated same-spelled variable in the caller.
(= (prime-need:make-local-capture)
   (delay (prime-need:capture-match $x)))
!(let $promise (prime-need:make-local-capture)
   (let $_ (force $promise)
     (noreduce-eq $x A)))

; A known-empty capture remains transitive and preserves call-time choice.
(= (prime-need:wrap-twice $x) (delay (delay $x)))
!(force
   (force
     (prime-need:wrap-twice (superpose (A B)))))

; Saved environments are opaque syntax but have exact logical support.
; A delayed restore must still see later refinement of those same identities.
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (let $promise (delay (superpose-bind $saved))
     (unify $x B (force $promise) Empty)))
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (let $promise (delay (superpose-bind $saved))
     (unify $x A (force $promise) Empty)))

; A saved range can introduce another variable which restoration refines.
!(chain (collapse-bind (unify $x (pair $y) token Empty)) $saved
   (let $promise (delay (chain (superpose-bind $saved) $_ $y))
     (unify $x (pair B) (force $promise) Empty)))

; Dependency summaries also include saved values inside another saved range.
!(chain (collapse-bind (unify $x A token Empty)) $first
   (chain (collapse-bind (unify $inner $first token Empty)) $second
     (let $promise
       (delay (chain (superpose-bind $second) $_ (superpose-bind $inner)))
       (unify $x B (force $promise) Empty))))

; A thunk stored as a saved binding value keeps its own lexical support.
!(let $inner (delay (unify $x A token Empty))
   (chain (collapse-bind (unify $v $inner saved Empty)) $saved
     (let $outer (delay (chain (superpose-bind $saved) $_ (force $v)))
       (unify $x B (force $outer) Empty))))
!(let $inner (delay (unify $x A token Empty))
   (chain (collapse-bind (unify $v $inner saved Empty)) $saved
     (let $outer (delay (chain (superpose-bind $saved) $_ (force $v)))
       (unify $x A (force $outer) Empty))))

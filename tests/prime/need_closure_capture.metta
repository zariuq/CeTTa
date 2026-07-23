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

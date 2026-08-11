; Prime contexts are immutable, parent-linked values.  Bindings are lazy and
; share one branch-local Need cell; keys are bare symbols or explicitly quoted
; closed structural names.

!(ctx:get ctx:empty x)
!(ctx:get (ctx:bind ctx:empty x 1) x)
!(ctx:get (ctx:bind (ctx:bind ctx:empty x 1) x 2) x)
!(let $base (ctx:bind ctx:empty x old)
   (let $extended (ctx:bind $base x new)
     (Pair (ctx:get $base x) (ctx:get $extended x))))

; Bare x and @x denote the same name.  Strings and compound expressions are
; names only when quoted; a raw compound stays uninterpreted.
!(ctx:get (ctx:bind ctx:empty @x quoted-symbol) x)
!(ctx:get (ctx:bind ctx:empty @"moo" quoted-string) @"moo")
!(ctx:get
   (ctx:bind ctx:empty @(mm-var "ph") structural-name)
   @(mm-var "ph"))
!(ctx:get ctx:empty (mm-var "ph"))

!(get-type ctx:empty)
!(get-type (ctx:bind ctx:empty x 1))

; Two reads share one call-time choice; the cross-product is forbidden.
!(let $ctx (ctx:bind ctx:empty choice (superpose (A B)))
   (Pair (ctx:get $ctx choice) (ctx:get $ctx choice)))

; A bounded view is newest-first and reifies source without forcing it.
!(bind! &ctx:counter (new-state 0))
(= (ctx:bump)
   (let $old (get-state &ctx:counter)
     (let $_ (change-state! &ctx:counter (+ $old 1)) $old)))
!(let $ctx (ctx:bind (ctx:bind ctx:empty x 1) y (ctx:bump))
   (ctx:view $ctx 1))
!(get-state &ctx:counter)
!(ctx:view (ctx:bind ctx:empty x 1) 0)
!(ctx:view (ctx:bind ctx:empty x 1) 1)

; The view is data, not another mutable or raw-spine context.
!(ctx:bind (ctx:view (ctx:bind ctx:empty x 1) 1) y 2)

; Explicit delay remains explicit.  Context lookup demands the hidden binding
; thunk, but never silently forces the first-class suspension it yields.
!(ctx:get
   (ctx:bind ctx:empty delayed (delay (superpose (A B))))
   delayed)
!(force
   (ctx:get
     (ctx:bind ctx:empty delayed (delay (superpose (A B))))
     delayed))

; A suspension closes over the context value present at allocation time.
!(let $base (ctx:bind ctx:empty x old)
   (let $suspended (delay (ctx:get $base x))
     (let $extended (ctx:bind $base x new)
       (force $suspended))))

; Persistence keeps graph identity: two reads after storage remain correlated.
!(bind! &ctx:store (new-space))
!(let $ctx (ctx:bind ctx:empty choice (superpose (A B)))
   (add-atom &ctx:store (StoredContext $ctx)))
!(match &ctx:store (StoredContext $saved)
   (Pair (ctx:get $saved choice) (ctx:get $saved choice)))
!(match &ctx:store (StoredContext $saved)
   (ctx:view $saved 1))

; A Prime registry name is a demanded value reference when it reaches a
; relation's value slot.  An unbound name remains ordinary data; resolving it
; must not be guessed during planning.
(= (ctx:reference-classify $candidate)
   (case $candidate
     (((RefBox $value) (ResolvedReference $value))
      ($_ (UnresolvedReference $candidate)))))
!(bind! &ctx:reference-value (RefBox resolved))
!(ctx:reference-classify &ctx:reference-value)
!(ctx:reference-classify &ctx:unbound-reference)

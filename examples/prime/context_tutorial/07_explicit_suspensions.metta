; Chapter 7: explicit suspensions remain explicit
;
; Hidden context-binding thunks are demanded by `ctx:get`.  A first-class
; suspension produced by `(delay e)` is different: lookup returns the
; suspension value, and only `force` evaluates its origin.

!(ctx:get
   (ctx:bind ctx:empty pending
     (delay (superpose (A B))))
   pending)

!(force
   (ctx:get
     (ctx:bind ctx:empty pending
       (delay (superpose (A B))))
     pending))

; One explicit suspension stored in a context retains one cell identity.
!(let $promise (delay (superpose (A B)))
   (let $context (ctx:bind ctx:empty pending $promise)
     (Pair
       (force (ctx:get $context pending))
       (force (ctx:get $context pending)))))

; Lexical capture is by context value.  A later sibling that shadows `model`
; cannot retroactively change the earlier suspension's environment.
!(let $base (ctx:bind ctx:empty model old)
   (let $pending (delay (ctx:get $base model))
     (let $newer (ctx:bind $base model new)
       (Triple
         (ctx:get $base model)
         (ctx:get $newer model)
         (force $pending)))))

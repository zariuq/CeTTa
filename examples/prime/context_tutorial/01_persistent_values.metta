; Chapter 1: contexts are persistent values
;
; A context is an immutable environment value.  `ctx:bind` does not update its
; input; it returns a new context with one newest binding.  Unlike a state cell,
; context identity does not require an `&name`, and the context API has no
; mutation operation.  A program may still store the whole context in a
; registry or space; doing so does not make its frames mutable.
;
; The conceptual representation is a persistent spine:
;
;   ctx:empty <- (policy = baseline) <- (policy = cautious)
;
;                       `----------- <- (policy = bold)
;
; Both children share the parent, but neither can change it.

; `context` is a distinct runtime type.
!(get-type ctx:empty)

; Lookup is total.  Absence is data that a program can match or propagate.
!(ctx:get ctx:empty absent-key)

; Extending a context returns a fresh value.  The old value remains usable.
!(let $base (ctx:bind ctx:empty policy baseline)
   (let $child (ctx:bind $base policy cautious)
     (Pair
       (ctx:get $base policy)
       (ctx:get $child policy))))

; Two children can coexist without copying or mutating their parent.
!(let $base (ctx:bind ctx:empty policy baseline)
   (let $left (ctx:bind $base policy cautious)
     (let $right (ctx:bind $base policy bold)
       (Triple
         (ctx:get $base policy)
         (ctx:get $left policy)
         (ctx:get $right policy)))))

; Keys may be closed structural names, not only bare symbols.
!(ctx:get
   (ctx:bind ctx:empty @(goal self-implies-self) assumed)
   @(goal self-implies-self))

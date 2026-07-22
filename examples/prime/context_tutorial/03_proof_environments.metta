; Chapter 3: contexts as object-level proof environments
;
; A proof checker conventionally carries a context Gamma of hypotheses.  Prime
; can represent such a Gamma as ordinary data without identifying runtime
; contexts with the type checker's own context by definition.

(= (ctx-proof:use $gamma $key)
   (ctx:get $gamma $key))

(= (ctx-proof:pair-intro $gamma $left-key $right-key)
   (let $left-proof (ctx-proof:use $gamma $left-key)
     (let $right-proof (ctx-proof:use $gamma $right-key)
       (proof pair-intro $left-proof $right-proof))))

; Introduce two assumptions and construct proof data from them.
!(let $gamma-p
     (ctx:bind ctx:empty hypothesis-p proof-p)
   (let $gamma-pq
       (ctx:bind $gamma-p hypothesis-q proof-q)
     (ctx-proof:pair-intro
       $gamma-pq hypothesis-p hypothesis-q)))

; The child has Q; its parent does not.  This is the operational shape needed
; for hypothetical introduction and later discharge.
!(let $parent
     (ctx:bind ctx:empty hypothesis-p proof-p)
   (let $child
       (ctx:bind $parent hypothesis-q proof-q)
     (let $parent-p (ctx-proof:use $parent hypothesis-p)
       (let $child-q (ctx-proof:use $child hypothesis-q)
         (let $parent-q (ctx-proof:use $parent hypothesis-q)
           (Triple $parent-p $child-q $parent-q))))))

; Shadowing is explicit and local.  An inner derivation may refine an
; assumption while the outer derivation retains the original evidence.
!(let $outer
     (ctx:bind ctx:empty hypothesis-p provisional-proof)
   (let $inner
       (ctx:bind $outer hypothesis-p checked-proof)
     (let $outer-proof (ctx-proof:use $outer hypothesis-p)
       (let $inner-proof (ctx-proof:use $inner hypothesis-p)
         (Pair $outer-proof $inner-proof)))))

; Closed structural names are useful when the object language itself supplies
; the key.  This direct lookup addresses a judgment-shaped assumption.
!(ctx:get
   (ctx:bind ctx:empty @(judgment P true) evidence-p)
   @(judgment P true))

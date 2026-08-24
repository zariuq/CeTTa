; Lowercase Prime List and proof-relevant relational lifting on one composed
; typing presentation.  The typed constructors and ordinary relational
; execution share the same authored vocabulary without making either one a
; prerequisite for the other.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(include &self ../../lib/ilp/prime_native_list_relator_types.metta)
!(import! &self ../../lib/ilp/prime_native_list_relator.metta)

!(add-atom &self (: sort-code (u 0)))
!(add-atom &self (: person-sort sort-code))
!(add-atom &self (: number-sort sort-code))
!(add-atom &self
  (: sort-step
    (-> sort-code (-> sort-code (u 0)))))
!(add-atom &self
  (: sort-next
    (-> sort-code sort-code)))
!(add-atom &self
  (: person-number-proof-a
    (sort-step person-sort number-sort)))
!(add-atom &self
  (: person-number-proof-b
    (sort-step person-sort number-sort)))

; The native family constructs its judgment rather than asking a checker to
; rediscover it after construction.
!(type:of
  (list:nil sort-code))
!(type:of
  (list:cons sort-code person-sort (list:nil sort-code)))
!(type:of
  (map-rel:nil sort-code sort-code sort-step))
!(type:of
  (map-rel:cons sort-code sort-code sort-step
    person-sort number-sort
    (list:nil sort-code) (list:nil sort-code)
    person-number-proof-a
    (map-rel:nil sort-code sort-code sort-step)))

; `map` is an ordinary checked Prime program, not a privileged constructor.
; Its four lambdas and the dependent eliminator are the exact term whose
; structure licenses the direct native beta/iota fold.
!(type:check
  (lam source
    (lam target
      (lam function
        (lam xs
          (list:eliminate source
            (lam ignored (list target))
            (list:nil target)
            (lam head
              (lam tail
                (lam induction
                  (list:cons target (function head) induction))))
            xs)))))
  (-> (source : (u 0))
      (target : (u 0))
      (function : (-> source target))
      (xs : (list source))
      (list target)))

; The same ordinary authored term now crosses the raw boundary once and then
; runs through Prime's language-owned native calculus.  The evaluator has no
; `map` primitive: exact recognition of the checked program licenses the
; fused List realization, whose applications remain ordinary MeTTa calls.
!((lam source
    (lam target
      (lam function
        (lam xs
          (list:eliminate source
            (lam ignored (list target))
            (list:nil target)
            (lam head
              (lam tail
                (lam induction
                  (list:cons target (function head) induction))))
            xs)))))
  sort-code sort-code sort-next
  (list:cons sort-code person-sort
    (list:cons sort-code number-sort (list:nil sort-code))))

; An ill-indexed proof is rejected by the native family formation itself.
!(type:of
  (map-rel:cons sort-code sort-code sort-step
    person-sort person-sort
    (list:nil sort-code) (list:nil sort-code)
    person-number-proof-a
    (map-rel:nil sort-code sort-code sort-step)))

; Raw relational execution remains available and retains both proof
; occurrences even though their endpoint lists coincide.
(= (rel:apply sort-step person-sort)
   (rel:edge number-sort person-number-proof-a))
(= (rel:apply sort-step person-sort)
   (rel:edge number-sort person-number-proof-b))

!(map-rel:run sort-step (person-sort))
!(map-rel:check sort-step (person-sort) (number-sort))
!(map-rel:check sort-step (person-sort) (person-sort))

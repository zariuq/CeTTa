; A proof-relevant grandparent program built from the native Prime indexed
; family `hyp`.  The family and its constructors are ordinary lowercase Prime
; declarations.  Prime may compile their checked structure, while primitive
; meanings remain ordinary relational MeTTa.

!(import! &self ../../lib/ilp/prime_native_hyp_types.metta)
!(import! &self ../../lib/ilp/prime_native_hyp.metta)

!(add-atom &self (: sort-code (u 0)))
!(add-atom &self (: person-sort sort-code))
!(add-atom &self (: number-sort sort-code))
!(add-atom &self
  (: primitive-vocabulary
    (-> sort-code (-> sort-code (u 1)))))

!(add-atom &self
  (: mother-symbol
    (primitive-vocabulary person-sort person-sort)))
!(add-atom &self
  (: father-symbol
    (primitive-vocabulary person-sort person-sort)))
!(add-atom &self
  (: successor-symbol
    (primitive-vocabulary number-sort number-sort)))
(: mother (-> u0 (-> u0 u1)))
(: father (-> u0 (-> u0 u1)))

; Sort codes map to carrier types independently of the primitive meanings.
; Distinct occurrences with the same endpoint remain distinct.
(= (hyp:carrier person-sort) u0)
(= (hyp:carrier number-sort) u0)
(= (hyp:meaning person-sort person-sort mother-symbol) mother)
(= (hyp:meaning person-sort person-sort father-symbol) father)

(= (mother alice) (rel:edge bob mother-alice-bob))
(= (mother alice) (rel:edge eve mother-alice-eve))
(= (father bob) (rel:edge carol father-bob-carol))
(= (father eve) (rel:edge carol father-eve-carol))

; Native construction: each lookup instantiates the declaration freshly, and
; the indexed result records the exact source and target sorts.
!(type:of
  (hyp:primitive sort-code primitive-vocabulary
    person-sort person-sort mother-symbol))
!(type:of
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort father-symbol)))

; A number relation cannot fill a person-to-person chain premise.
!(type:of
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      number-sort number-sort successor-symbol)))

; The checked `hyp` structure licenses a native composition plan.  Its
; language-authored primitive relations still run relationally, retaining the
; shared middle and both proof occurrences.
!(hyp:run
  (quote
    (hyp:chain sort-code primitive-vocabulary
      person-sort person-sort person-sort
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort mother-symbol)
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort father-symbol)))
  alice)

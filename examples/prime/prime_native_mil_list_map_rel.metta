; Learn an ordinary typed `hyp` chain, expose it as a named relation, then
; reuse the proof-relevant List relator.  Equal endpoint lists retain every
; distinct elementwise derivation occurrence.

!(import! &self ../../lib/ilp/prime_native_mil_list.metta)

(: sort-code (u 0))
(: person-sort sort-code)
(: number-sort sort-code)
(: primitive-vocabulary
   (-> sort-code (-> sort-code (u 1))))

(: mother-symbol
   (primitive-vocabulary person-sort person-sort))
(: father-symbol
   (primitive-vocabulary person-sort person-sort))
(: successor-symbol
   (primitive-vocabulary number-sort number-sort))

(: mother (-> u0 (-> u0 u1)))
(: father (-> u0 (-> u0 u1)))
(: successor (-> u0 (-> u0 u1)))

(: alice u0)
(: bob u0)
(: carol u0)
(: eve u0)
(: zero u0)
(: one u0)
(: mother-alice-bob (mother alice bob))
(: mother-alice-eve (mother alice eve))
(: father-bob-carol (father bob carol))
(: father-eve-carol (father eve carol))
(: successor-zero-one (successor zero one))

(= (hyp:carrier person-sort) u0)
(= (hyp:carrier number-sort) u0)
(= (hyp:meaning person-sort person-sort mother-symbol) mother)
(= (hyp:meaning person-sort person-sort father-symbol) father)
(= (hyp:meaning number-sort number-sort successor-symbol) successor)

(= (mother alice) (rel:edge bob mother-alice-bob))
(= (mother alice) (rel:edge eve mother-alice-eve))
(= (father bob) (rel:edge carol father-bob-carol))
(= (father eve) (rel:edge carol father-eve-carol))
(= (successor zero) (rel:edge one successor-zero-one))

(= (native-ho:scalar-examples)
   (hyp:examples
     ((hyp:example alice carol))
     ((hyp:example alice bob))))

(= (native-ho:list-examples)
   (rel:list-examples
     ((rel:list-example (alice) (carol))
      (rel:list-example (alice alice) (carol carol)))
     ((rel:list-example (alice) (bob)))))

; The learned program is an ordinary first-class relation value.  `map-rel`
; remains unaware of learning and `hyp` remains unaware of List; no generated
; name or mutation of the language presentation is needed.
(= (native-ho:learned-map-rel $source-list)
   (let $program
        (hyp:learn-chain
          &self sort-code primitive-vocabulary
          person-sort person-sort (native-ho:scalar-examples))
     (map-rel:run (hyp:relation $program) $source-list)))

; The List relator consumes the relational meaning of a checked `hyp`
; program without adding a lifting constructor to the hypothesis family.
!(native-ho:learned-map-rel (alice))

!(hyp:chain-benchmark
  native-list-map-rel
  (native-hyp
    (relator list)
    (candidate-bag exact)
    (proof-relevant True)
    (checker-provenance runtime-stats))
  &self sort-code primitive-vocabulary
  person-sort person-sort (native-ho:scalar-examples))

!(hyp:learn-chain
  &self sort-code primitive-vocabulary
  person-sort person-sort (native-ho:scalar-examples))

!(let $program
      (hyp:learn-chain
        &self sort-code primitive-vocabulary
        person-sort person-sort (native-ho:scalar-examples))
   (rel:list-consistent
     (hyp:relation $program) (native-ho:list-examples)))

; One input has two derivations with the same endpoint List.
!(collapse (native-ho:learned-map-rel (alice)))

; Two inputs independently branch, retaining all four proof combinations.
!(list:len
  (collapse
    (native-ho:learned-map-rel (alice alice))))

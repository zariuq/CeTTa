; A complete finite chain learner over ordinary Prime `hyp` values.
;
; The untyped baseline enumerates every pair in the authored vocabulary.  The
; typed producer joins primitive declarations on their shared family index,
; examples choose the useful inhabitant, and Prime's native calculus executes
; the resulting proof program without a special MIL evaluator.

!(import! &self ../../lib/ilp/prime_native_mil.metta)

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

(= (rel:apply mother alice) (rel:edge bob mother-alice-bob))
(= (rel:apply mother alice) (rel:edge eve mother-alice-eve))
(= (rel:apply father bob) (rel:edge carol father-bob-carol))
(= (rel:apply father eve) (rel:edge carol father-eve-carol))
(= (rel:apply successor zero) (rel:edge one successor-zero-one))

(= (native-gp:examples)
   (hyp:examples
     ((hyp:example alice carol))
     ((hyp:example alice bob))))

!(type:check
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort father-symbol))
  (hyp sort-code primitive-vocabulary person-sort person-sort))

; This raw candidate exposes the shared-middle mismatch.  It is a checked
; refutation in the named fragment, not an absent answer from example search.
!(type:check
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      number-sort number-sort successor-symbol))
  (hyp sort-code primitive-vocabulary person-sort person-sort))

!(hyp:outputs
  (quote
    (hyp:chain sort-code primitive-vocabulary
      person-sort person-sort person-sort
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort mother-symbol)
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort father-symbol)))
  alice)

!(hyp:consistent
  (quote
    (hyp:chain sort-code primitive-vocabulary
      person-sort person-sort person-sort
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort mother-symbol)
      (hyp:primitive sort-code primitive-vocabulary
        person-sort person-sort father-symbol)))
  (native-gp:examples))

!(hyp:chain-benchmark
  native-grandparent
  (native-hyp
    (candidate-bag exact)
    (proof-relevant True)
    (checker-provenance runtime-stats))
  &self sort-code primitive-vocabulary
  person-sort person-sort (native-gp:examples))

!(hyp:learn-chain
  &self sort-code primitive-vocabulary
  person-sort person-sort (native-gp:examples))

!(let $program
     (hyp:learn-chain
       &self sort-code primitive-vocabulary
       person-sort person-sort (native-gp:examples))
   (collapse (hyp:run $program alice)))

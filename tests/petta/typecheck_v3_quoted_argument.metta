; Quoted code is constructed deterministically when passed as Atom data.
(: RuntimeAddition (Newtype Atom))
(: rules (-[det]-> Atom Atom))

(: build-runtime-rule
   (-[det]-> Atom Atom RuntimeAddition))
(= (build-runtime-rule $premises $conclusion)
   (brand RuntimeAddition
      (rules (quote ($premises |- $conclusion)))))

; A tuple with no callable head is data constructed once, even when retained
; behind a nominal marker.
(: CompiledAddition (Newtype Atom))
(: build-inert-rule
   (-[det]-> Atom Atom CompiledAddition))
(= (build-inert-rule $premises $conclusion)
   (brand CompiledAddition
      (() |- ($premises $conclusion))))

; Nominal construction must not erase real multiplicity in its payload.
(: reject-nondet-brand (-[det]-> CompiledAddition))
(= (reject-nondet-brand)
   (brand CompiledAddition
      (superpose (1 2))))

; A fresh logic variable is an executable value whose type remains gradual.
(: pair (-[det]-> Atom Atom Atom))
(: build-fresh-pair (-[det]-> Atom))
(= (build-fresh-pair)
   (pair $left $right))

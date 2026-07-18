; Omitting the resource argument requests unbounded search. An explicit
; positive resource argument requests a bounded producer prefix.
(: PrimeUnboundedBase Type)
(: PrimeUnboundedGoal Type)
(: prime-unbounded-base PrimeUnboundedBase)
(: prime-unbounded-step (-> PrimeUnboundedBase PrimeUnboundedGoal))
(chaining-rule prime-unbounded-step)

; A finite prefix reports incompleteness rather than logical absence.
!(search-first-inhabitant &self PrimeUnboundedGoal 1 1)

; Resource omission permits the terminating search to finish.
!(search-first-inhabitant &self PrimeUnboundedGoal 1)

; A completed search at the requested depth can establish genuine absence.
!(search-first-inhabitant &self PrimeUnboundedMissing 1)

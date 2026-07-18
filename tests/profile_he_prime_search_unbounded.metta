; Omitting the resource argument is a genuinely unbounded search policy.
; Explicit positive fuel remains the backward-compatible bounded policy.
(: UnboundedBase Type)
(: UnboundedGoal Type)
(: unbounded-base UnboundedBase)
(: unbounded-step (-> UnboundedBase UnboundedGoal))
(chaining-rule unbounded-step)

; This finite prefix is too small and must report incompleteness, not absence.
!(search-first-inhabitant &self UnboundedGoal 1 1)

; The same query succeeds when the caller omits the resource bound.
!(search-first-inhabitant &self UnboundedGoal 1)

; A completed search at the requested depth may still reject genuine absence.
!(search-first-inhabitant &self UnboundedMissing 1)

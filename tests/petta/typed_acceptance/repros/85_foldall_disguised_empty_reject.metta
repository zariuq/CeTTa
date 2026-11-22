; A superpose whose only branch is empty produces no elements, so foldall
; returns its initializer.
; The accumulator's non-empty result cannot certify a distinct declared result
; type for this case.  The permissive mode leaves a runtime obligation; strict
; mode rejects that unresolved obligation.

(: step (-> String Number Number))
(= (step $state $item) $item)

(: bad-fold (-> String Number))
(= (bad-fold $state)
   (foldall step (superpose ((empty))) $state))

; Executable witnesses for the HE SearchStrategy boundary.  Exhaustion and an
; answer limit are incomplete observations; only a completed finite search may
; establish that its requested depth contains no inhabitant.

(= (enumeration-status
     (typing-search resource-incomplete $depth $reason $proofs $stats))
   incomplete)
(= (enumeration-status
     (typing-search more-possible $depth $reason $proofs $stats))
   incomplete)
(= (enumeration-status
     (typing-search depth-complete $depth $reason $proofs $stats))
   complete)

(= (first-status (he-unknown $reason)) incomplete)
(= (first-status (he-accept $witness)) found)
(= (first-status (he-reject (no-inhabitant-at-depth))) complete-empty)

(= (forward-step-status
     (he-accept (forward-step $added $found resource-incomplete $stats)))
   incomplete)
(= (forward-step-status
     (he-accept (forward-step $added $found more-possible $stats)))
   incomplete)
(= (forward-step-status
     (he-accept (forward-step $added $found complete $stats)))
   complete)

(= (forward-closure-status
     (he-accept (forward-closure $rounds $added resource-incomplete)))
   incomplete)
(= (forward-closure-status
     (he-accept (forward-closure $rounds $added rounds-incomplete)))
   incomplete)
(= (forward-closure-status
     (he-accept (forward-closure $rounds $added fixpoint)))
   complete)

; Positive controls on the empty rule set.
!(assertEqual
   (forward-step-status (type-forward-step &self 10000 10))
   complete)
!(assertEqual
   (forward-closure-status (type-forward-closure &self 1 10000 10))
   complete)

(: SearchBase Type)
(: SearchGoal Type)
(: search-base SearchBase)
(: search-step (-> SearchBase SearchGoal))
(chaining-rule search-step)

; Each bounded exhaustion path stays incomplete.
!(assertEqual
   (enumeration-status
     (search-inhabitants &self SearchGoal 1 1 2))
   incomplete)
!(assertEqual
   (first-status
     (search-first-inhabitant &self SearchGoal 1 1))
   incomplete)
!(assertEqual
   (forward-step-status (type-forward-step &self 1 10))
   incomplete)
!(assertEqual
   (forward-closure-status (type-forward-closure &self 3 1 10))
   incomplete)

; More resources find the witness, and exhaustive absence is distinguished
; from budget exhaustion.
!(assertEqual
   (enumeration-status
     (search-inhabitants &self SearchGoal 1 10000 2))
   complete)
!(assertEqual
   (first-status
     (search-first-inhabitant &self SearchGoal 1))
   found)
!(assertEqual
   (first-status
     (search-first-inhabitant &self MissingGoal 1))
   complete-empty)

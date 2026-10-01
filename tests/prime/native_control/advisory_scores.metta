(= (score-loop) (score-loop))
(= (finite) (Item a 1))
; Diverging scheduling metadata cannot block this answer or finite closure.
!(select 2 (search-policy (control WO
   (key $candidate (score-loop)) (budget 1000))) (finite))
!(select 1 (search-policy (control WO
   (key $candidate (score-loop)) (best (Item $id $cost) $cost)
   (budget 1000))) (finite))

(= (slow $n) (if (== $n 0) Done (slow (- $n 1))))
(= (rank $n) (+ (+ $n 0) 0))
(= (jobs) (Job 4 (slow 100)))
(= (jobs) (Job 1 (slow 100)))
; The completed score updates the existing occurrence's scheduling metadata.
!(select 1 (search-policy (control WO
   (key (Job $cost $work) (rank $cost)) (budget 20000))) (jobs))

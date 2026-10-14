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
; A late score reaches the work that still inherits it. Each lane's body
; keeps running under the inherited key while its score is computed.
(= (race) (Lane 1 Low (slow 300)))
(= (race) (Lane 0 High (slow 300)))
!(select 1 (search-policy (control FIFO (budget 100000))) (race))
!(select 1 (search-policy (control WO (key (Lane $r $id $work) $r) (budget 100000))) (race))
!(select 1 (search-policy (control WO (key (Lane $r $id $work) (rank $r)) (budget 100000))) (race))
!(select 2 (search-policy (control WO (key (Lane $r $id $work) (rank $r)) (budget 100000))) (race))
; A descendant with a key of its own keeps it when an ancestor's late score
; arrives.
(= (inner $k) (Lane $k Inner (slow 300)))
(= (nested) (Lane 1 Low (inner 1)))
(= (nested) (Lane 0 High (inner 5)))
!(select 1 (search-policy (control WO (key (Lane $r $id $work) $r) (budget 100000))) (nested))
!(select 1 (search-policy (control WO (key (Lane $r $id $work) (rank $r)) (budget 100000))) (nested))

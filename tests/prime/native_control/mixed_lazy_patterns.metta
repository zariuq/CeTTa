; A lazy wildcard result is independent of the demanded pattern's stream.
(= (generate $n) (generate (+ $n 1)))
(= (generate $n) (Box $n))
(= (project $x) 7)
(= (project (Box $n)) $n)
!(select 7 (search-policy (control FIFO (budget 3000))) (project (generate 0)))

; The wildcard activation occurs once, including a strict nonmatch branch.
(= (finite) (Box 1))
(= (finite) (Box 2))
(= (finite) (Other 3))
!(select 9 (search-policy (control FIFO (budget 3000))) (project (finite)))

; A body retaining the shared argument needs descendant-world reconciliation.
(= (expose $x) (Pair $x $x))
(= (expose (Box $n)) $n)
!(select 9 (search-policy (control FIFO (budget 3000))) (expose (finite)))

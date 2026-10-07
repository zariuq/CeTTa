; A complete finite-prefix observation of an infinite relation.
(= (walk $n) (walk (S $n)))
(= (walk $n) (answer $n))

!(select 13 (walk Z))

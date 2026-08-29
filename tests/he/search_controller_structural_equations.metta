(= (lag Z) (answer deep))
(= (lag (S $n)) (lag $n))
(= (lag (S $n)) (dead $n))

(= (probe) (lag (S (S (S (S (S (S (S (S Z))))))))))
(= (probe) (answer shallow))

!(probe)

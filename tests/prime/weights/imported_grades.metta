; One physical imported row duplicates the caller's own row.
(=w (import-choice) 2 3)
(= (import-choice) 5 (where 7))
(= (import-pair $x) (Pair $x $x))
(= (weigh-import $x) $x (where $x))
(import-fact 2)
(= (import-wait) (import-wait))

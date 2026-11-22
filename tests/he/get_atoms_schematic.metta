!(bind! &schemas (new-space))
!(add-atom &schemas (r $p))
!(bind! &pairs (new-space))
; Independent retrievals are kept as data in one row. Their full identities
; may have the same coordinate in distinct source frames.
!(let* (((r $a) (get-atoms &schemas)) ((r $b) (get-atoms &schemas)))
   (add-atom &pairs (pair $a $b)))
!(collapse (let (pair 1 2) (get-atoms &pairs) independent))
; An alias within one stored row remains an alias after retrieval.
!(bind! &aliases (new-space))
!(add-atom &aliases (pair $x $x))
!(collapse (let (pair 1 1) (get-atoms &aliases) shared))
!(collapse (let (pair 1 2) (get-atoms &aliases) split))
; Retrieving again after a caller instantiated its previous row is fresh.
(= (use $v Z) (let* (((r $a) (get-atoms &schemas))) (let $v $a done)))
(= (use $v (S $k)) (let* (((r $a) (get-atoms &schemas))) (let $v $a (use 2 $k))))
!(collapse (use 1 (S Z)))

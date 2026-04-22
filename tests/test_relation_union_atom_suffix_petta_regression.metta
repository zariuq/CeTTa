(= (mylast $x) (union-atom $xs ($x)))
(= (init $xs) (union-atom $xs ($x)))
(= (rcons $xs $x) (union-atom $xs ($x)))

!(test (let (mylast $x) (1 2 3) $x) 3)
!(test (let (init $xs) (1 2 3) $xs) (1 2))
!(test (let (rcons $xs $x) (1 2 3) ($xs $x)) ((1 2) 3))

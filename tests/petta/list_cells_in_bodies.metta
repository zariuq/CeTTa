; union-atom (append/3) and cons-atom read a cons cell as the list cell it
; is, written at the top or reached through a function's parameters: a
; chain that closes is the flat list it spells, and an improper one is no
; list (SWI-PeTTa's answers).
(= (ua $v $s) (union-atom $v $s))
(= (ca $h $t) (cons-atom $h $t))
!(collapse (union-atom (cons 1 foo) ()))
!(collapse (ua (cons 1 foo) ()))
!(collapse (ua (3) (cons 1 foo)))
!(collapse (ua (cons 1 (cons 2 ())) (3)))
!(collapse (ua (3) (cons 1 (cons 2 ()))))
!(collapse (ca 0 (cons 1 foo)))
!(collapse (ca 0 (cons 1 (cons 2 ()))))
!(foldall ua (superpose ((cons 1 foo) (3))) ())
!(foldall union-atom (superpose ((cons 1 foo) (3))) ())
!(foldall (|-> ($v $s) (union-atom $v $s)) (superpose ((cons 1 foo) (3))) ())

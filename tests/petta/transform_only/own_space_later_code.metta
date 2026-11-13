; Code held as data and run later reads the program's own space, and functions
; defined in parallel branches are all there afterwards.
(weight 1)
(weight 2)
!(test (collapse (eval (match &self (weight $w) $w))) (1 2))
!(test (collapse (eval (foldall + (match &self (weight $w) $w) 0))) (3))
!(test (let $r (collapse (hyperpose ((add-atom &self (= (tenfold $v) (* $v 10)))
                                     (add-atom &self (= (less100 $v) (- $v 100))))))
            done)
       done)
!(test (collapse (tenfold 2)) (20))
!(test (collapse (less100 5)) (-95))
!(test (size-atom (collapse (get-atoms &self))) 4)

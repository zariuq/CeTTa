; PeTTa's collapse is findall: each collected answer is a copy.  Its
; unbound variables are fresh, the caller's included, and are shared only
; within that answer.  Each check binds the copies afterwards, so the
; observation is ground.
(= (f) (p $z))
(= (g) (p $z $z))
(= (h $y) (collapse (superpose ((q $y) (q $y)))))

; Two answers never share a variable.
!(test (let $c (collapse (superpose ((f) (f))))
         (let ((p $x) (p $y)) $c (let $x 1 (let $y 2 $c))))
       ((p 1) (p 2)))

; One answer keeps its own sharing.
!(test (let $c (collapse (g)) (let ((p $x $y)) $c (let $x 1 $c)))
       ((p 1 1)))

; The caller's variable is copied: binding it later does not reach into the
; collection, and the two copies are independent.
!(test (let $v (q $y)
         (let $c (collapse (superpose ($v $v)))
           (let $y 0 (let ((q $a) (q $b)) $c (let $a 1 (let $b 2 $c))))))
       ((q 1) (q 2)))
!(test (let $c (h $w)
         (let $w 0 (let ((q $a) (q $b)) $c (let $a 1 (let $b 2 $c)))))
       ((q 1) (q 2)))

; A variable bound before the collection is copied with its value.
!(test (let $y 1 (collapse (superpose ((q $y) (q $y))))) ((q 1) (q 1)))

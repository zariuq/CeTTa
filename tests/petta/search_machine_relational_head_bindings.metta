; The body of a relationally opened equation reads the bindings its head
; made.  Each equation below takes its list through (cons $p $rest), which
; structural matching does not discharge, so the equation is opened by
; solving its head against the call.  Before the call, the query binds one of
; its own variables to a fresh copy, so the environment already holds a
; binding when each equation opens, and the box carries an open field.
; Every call answers, as it does with a ground box or without the copy.
(= (pick (box $s $v)) $s)
(= (sum-over $w ()) 0)
(= (sum-over $w (cons $p $rest)) (+ (pick $w) (sum-over $w $rest)))
(= (add-box $acc (box $s $v)) (+ $acc $s))
(= (fold-box $acc $w ()) $acc)
(= (fold-box $acc $w (cons $p $rest)) (fold-box (add-box $acc $w) $w $rest))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Sum (sum-over (box 5 $q) (x y))))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Fold (fold-box 0 (box 5 $q) (x y z))))
!(let $c (copy_term (pair $a)) (let (pair $f) $c (Nested (fold-box 0 (box 2 $q) (x y)))))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Ground (fold-box 0 (box 5 v) (x y))))
!(Plain (fold-box 0 (box 5 $q) (x y)))

; let* is nested lets, whose patterns are expressions: a pattern that
; computes, such as (cons $h $t), is evaluated and then meets its value, in
; a query and in an equation's body alike.
(= (split $l) (let* (((cons $h $t) $l)) ($h $t)))
(= (split2 $l) (let* (($z 0) ((cons $h (cons $h2 $t)) $l) ($w (+ $z 1))) ($h $h2 $t $w)))
!(let* (((cons $h $t) (a b c))) ($h $t))
!(let* (((cons $h $t) (a b c)) ($z 1)) ($h $t $z))
!(let* (($z 1) ((cons $h $t) (a b c))) ($h $t $z))
!(let* (((cons $h (cons $h2 $t)) (a b c))) ($h $h2 $t))
!(let* (((cons $h $t) (quote (a b c)))) ($h $t))
!(let* ((($h $t) (a b))) ($h $t))
!(split (a b c))
!(split2 (1 2 3 4))
!(split ())
!(collapse (let* (((cons $h $t) (superpose ((a b) (c d))))) ($h $t)))

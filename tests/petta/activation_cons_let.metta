; A let whose pattern computes a list cell, in the body of a function whose
; head holds a relational pattern: the pattern's cell meets the value as the
; list it spells, in whichever order the two are solved.
(= (as $a $b) (let $a $b $a))
(= (type? $T) true)
(= (f (as $s $T)) (let ((cons P $l) $r) $T x))
(= (g $kb (as $stmt (: $prf $Type $tv))) (if (type? $Type) (let ($h (cons P $l) (cons C $r)) $Type x)))
(= (f0 $T) (let ((cons P $l) $r) $T x))
(= (fl (as $s $T)) (let (Q (cons P $l)) $T (l $l)))
(= (fa (as $s $T)) (let ((cons P $l) $r) $T (a $l $r)))
!(collapse (f ((P) (C))))
!(collapse (g bk (: a (Bi (P) (C)) t)))
!(collapse (f0 ((P) (C))))
!(collapse (fl (Q (P R))))
!(collapse (fa ((P Q) (C))))
!(let $j ((cons P $l) $r) (let $j ((P) (C)) (ok $l $r)))
!(let $j ((P) (C)) (let $j ((cons P $l) $r) (ok $l $r)))

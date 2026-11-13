; Equations told apart by literals inside their arguments.  The equations a
; call's arguments leave each answer; an unbound variable or a
; list built with cons in the argument leaves every equation to matching,
; and a different structure leaves only those that test nothing there.

(= (pos (x $a $b)) first)
(= (pos ($a x $b)) second)
(= (pos ($a $b x)) third)
(= (pos (x x $b)) both)
(= (pos (y $a $b)) other)
(= (pos $any) any)

!(sort-atom (collapse (pos (x o o))))
!(sort-atom (collapse (pos (x x o))))
!(sort-atom (collapse (pos (o o x))))
!(sort-atom (collapse (pos (x x x))))
!(collapse (pos (o o o)))
!(collapse (pos (x o)))
!(collapse (pos foo))
!(sort-atom (collapse (let $v (pos ($q o x)) (let $q z $v))))
!(sort-atom (collapse (pos (cons x (cons o (cons o ()))))))

(= (deep ((a $x) $y)) one)
(= (deep (($x b) $y)) two)
(= (deep ($x (c $y))) three)
(= (deep (($x $x) $y)) same)

!(sort-atom (collapse (deep ((a b) (c d)))))
!(collapse (deep ((a q) (e d))))
!(sort-atom (collapse (deep ((b b) (c c)))))
!(collapse (deep (a (c d))))

; A literal compared with a number of the other kind.
(= (num (1 $x)) int)
(= (num (1.0 $x)) float)
(= (num (2 $x)) two)
!(collapse (num (1 z)))
!(collapse (num (1.0 z)))
!(collapse (num (3 z)))

; Literals in the argument itself and in its first element, told apart by
; the argument's functor: an atom, or an expression's length with its first
; element.  A number of the other kind, an unbound first element, a length
; no equation reads and a list built with cons each keep their answers.
(= (shape red) atom-red)
(= (shape 3) atom-three)
(= (shape (pair $x $y)) pair)
(= (shape (red $x)) red-head)
(= (shape (3 $x)) three-head)
(= (shape ($f $x)) any-head)
(= (shape (triple $x $y $z)) triple)

!(collapse (shape red))
!(collapse (shape blue))
!(collapse (shape 3))
!(collapse (shape 3.0))
!(collapse (shape (pair 1 2)))
!(sort-atom (collapse (shape (red 1))))
!(collapse (shape (blue 1)))
!(sort-atom (collapse (shape (3 1))))
!(collapse (shape (3.0 1)))
!(collapse (shape (pair 1 2 3)))
!(collapse (shape ()))
!(sort-atom (collapse (let $v (shape ($h 1)) (let $h red $v))))
!(sort-atom (collapse (let $v (shape $q) (let $q (red 1) $v))))
!(sort-atom (collapse (shape (cons red (cons 1 ())))))

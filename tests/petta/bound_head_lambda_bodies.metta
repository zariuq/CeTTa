; A lambda body whose head is bound at run time reads it as an equation body
; does: data when the head's value is a number or a string, a call when it
; names a function, a function applied in part, or a closure.  A lambda of
; three parameters runs as one body over all of its arguments.
(= (add7 $x) (+ $x 7))
(= (addk $k $x) (+ $x $k))
(= (apply3 $l $a $b $c) ($l $a $b $c))
!(apply3 (|-> ($f $x $unused) ($f $x)) add7 3 0)
!(apply3 (|-> ($f $x $unused) ($f $x)) (addk 7) 3 0)
!(apply3 (|-> ($f $x $unused) ($f $x)) (|-> ($y) (* $y 2)) 3 0)
!(apply3 (|-> ($f $x $unused) ($f 3)) add7 0 0)
!(apply3 (|-> ($f $x $unused) (id ($f $x))) add7 3 0)
!(apply3 (|-> ($f $x $unused) ($f $x)) 1 2 0)
!(apply3 (|-> ($f $x $unused) ($f $x)) "s" 2 0)

; The same lambdas made inside an equation.
(= (via3 $f $x) (apply3 (|-> ($g $y $unused) ($g $y)) $f $x 0))
(= (via3-literal $f) (apply3 (|-> ($g $y $unused) ($g 3)) $f 0 0))
!(via3 add7 3)
!(via3 (addk 7) 3)
!(via3 1 2)
!(via3-literal add7)

; A closure over both the function and its argument, made inside an
; equation and applied elsewhere.
(= (apply1 $l $x) ($l $x))
(= (later $x $f) (apply1 (|-> ($ignored) ($f $x)) done))
!(later 3 add7)
!(later 3 (addk 7))
!(let $k 7 (later 3 (|-> ($a) (+ $a $k))))

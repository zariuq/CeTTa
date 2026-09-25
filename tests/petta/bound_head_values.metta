; An equation's argument pattern with a variable head is structure: it
; matches a list of that length and is never evaluated.
(= (swap ($a $b)) ($b $a))
!(swap (1 2))
!(swap (a (b c)))

; A body expression whose head is bound at run time is data when the head's
; value is a number or a string, and a call when it names a function.  The
; argument is evaluated first, so (inc 2) arrives as 3 and does not match.
(= (inc $x) (+ $x 1))
(= (apply-head ($f $x)) ($f $x))
!(apply-head (1 2))
!(apply-head (inc 2))
!(apply-head ("s" 2))

; A head computing an integer makes the expression data over its fields.
(= (mk $t) ((+ $t 1) 1))
!(mk 2)

; Both together, in a step of the kind a counting loop takes.
(= (step ($t $i $s))
   (if (== $i $t) ((+ $t 1) 1 (+ $s $t)) ($t (+ $i 1) (+ $s $i))))
!(step (2 1 0))
!(step (2 2 5))

(= (three ($f $x $y)) (matched $f $x $y))
!(three (1 2 3))

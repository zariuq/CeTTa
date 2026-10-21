; HE answers a value once: a value its evaluation answered is not evaluated
; again.  fr's two equations leave a call unreduced, so evaluating (fr a)
; answers it twice, and evaluating it again would answer it twice more.
(= (fr $x) NotReducible)
(= (fr $x) NotReducible)
(= (g $x) (wrap $x))
(= (g1 $x) (g $x))
(= (coin) 0)
(= (coin) 1)
(= (twice $x) (pair $x $x))
(: twicem (-> Atom %Undefined%))
(= (twicem $x) (pair $x $x))
(= (pairit $x) (pair $x (fr a)))
(= (g2 $x) (let $u (add-atom &self (= (later) done)) $x))
(= (f x) reduced)
(= (pair-of $a $b) NotReducible)
(= (one-of $a) NotReducible)
; A one-line function answers what its body answers.
!(wrap (fr a))
!(g (fr a))
!(g1 (fr a))
; A value argument is chosen once and shared; an Atom parameter is chosen
; where it is used.
!(twice (coin))
!(twicem (coin))
!(twice (fr a))
; A value is not evaluated again, while code written in a body is.
!(pairit (fr a))
!(let $v (fr a) (wrap $v))
; Collecting answers and spreading them again gives the same answers.
!(let $c (collapse (fr a)) (superpose $c))
!(unique (g1 (fr a)))
; A space holds syntax: a value stored and matched back is evaluated again.
!(let $v (fr a) (add-atom &self (stored $v)))
!(match &self (stored $w) (wrap $w))
; A value keeps the form it was answered in.
!(g2 (later))
!(later)
; A new expression built from a value's parts is code: the rest of a value
; and a part joined to it are evaluated where they are used.
!(let $l (pair-of f x) (let $t (cdr-atom $l) $t))
!(let $l (one-of x) (let $t (cdr-atom $l) (let $c (cons-atom f $t) $c)))
; A state cell keeps a value; a space it is stored into keeps syntax.
!(let $v (fr a) (let $s (new-state $v) (wrap (get-state $s))))
!(let $v (fr a) (let $s (new-state $v) (let $w (get-state $s) (add-atom &self (kept $w)))))
!(match &self (kept $x) (wrap $x))

; Updating a cell and storing a compound value keep evaluated fields too.
!(let $v (fr a) (let $s (new-state $v)
   (let $u (change-state! $s $v) (wrap (get-state $s)))))
!(let $v (fr a) (let $s (new-state (Box $v)) (wrap (get-state $s))))

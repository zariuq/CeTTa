; HE returns a collected tuple and a set operation's result as they are, and
; evaluates such a value again wherever a binder instantiates it into syntax
; it evaluates: a let body, an argument, a branch, a callee's body.  Where a
; set operation takes it, where add-atom stores it, and as a call's answer,
; it stays as it is.

(= (f $x) (Called $x))
(= (g $xs) (Box $xs))

; Read in a let body, as the body, and in a match's collection.
(= (in-body) (let $c (collapse (superpose (f g))) (Got $c)))
!(in-body)
(= (as-body) (let $c (collapse (superpose (f g))) $c))
!(as-body)
(k f)
(k g)
(= (matched) (let $c (collapse (match &self (k $x) $x)) (Got $c)))
!(matched)

; A call's answer stays as it is, and is evaluated where a let reads it.
(= (answer) (collapse (superpose (f g))))
!(answer)
!(let $c (answer) $c)

; An argument evaluated once stays; a callee's body reads its parameter.
!(Got (collapse (superpose (f g))))
!(g (collapse (superpose (f g))))

; Set operations take their tuples as they are.
(= (unioned) (let $c (collapse (superpose (f g))) (union-atom $c ())))
!(unioned)
(= (subtracted) (let $c (collapse (superpose (f g))) (subtraction-atom $c ())))
!(subtracted)
(= (unique-of) (let $c (collapse (superpose (f g))) (unique-atom $c)))
!(unique-of)

; Comparison and branches evaluate it.
(= (compared) (let $c (collapse (superpose (f g))) (== $c (Called g))))
!(compared)
(= (branched) (let $c (collapse (superpose (f g))) (if True $c no)))
!(branched)

; Stored as it is; a match template evaluates the stored row.
(= (stored-it) (let $c (collapse (superpose (f g))) (add-atom &self (stored $c))))
!(stored-it)
!(match &self (stored $s) $s)

; A tuple of values evaluates to itself.
(n 1)
(n 2)
(= (numbers) (let $c (collapse (match &self (n $x) $x)) (Got $c)))
!(numbers)

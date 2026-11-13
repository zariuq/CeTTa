; HE returns a collected tuple and a set operation's result as they are, and
; evaluates such a value again wherever a binder instantiates it into syntax
; it evaluates: a let body, an argument, a branch, a callee's body.  Where a
; set operation takes it, where add-atom stores it, and as a call's answer,
; it stays as it is.  The collected answers are identical, so the tuple is
; the same in whatever order a collapse lists them.

(= (f $x) (Called $x))
(= (g $xs) (Box $xs))

; Read in a let body, as the body, and in a match's collection.
(= (in-body) (let $c (collapse (superpose (f f))) (Got $c)))
!(in-body)
(= (as-body) (let $c (collapse (superpose (f f))) $c))
!(as-body)
(k f)
(k f)
(= (matched) (let $c (collapse (match &self (k $x) $x)) (Got $c)))
!(matched)

; A call's answer stays as it is, and is evaluated where a let reads it.
(= (answer) (collapse (superpose (f f))))
!(answer)
!(let $c (answer) $c)

; An argument evaluated once stays; a callee's body reads its parameter.
!(Got (collapse (superpose (f f))))
!(g (collapse (superpose (f f))))

; Set operations take their tuples as they are.
(= (unioned) (let $c (collapse (superpose (f f))) (union-atom $c ())))
!(unioned)
(= (subtracted) (let $c (collapse (superpose (f f))) (subtraction-atom $c ())))
!(subtracted)
(= (unique-of) (let $c (collapse (superpose (f f))) (unique-atom $c)))
!(unique-of)

; Comparison and branches evaluate it.
(= (compared) (let $c (collapse (superpose (f f))) (== $c (Called f))))
!(compared)
(= (branched) (let $c (collapse (superpose (f f))) (if True $c no)))
!(branched)

; Stored as it is; a match template evaluates the stored row.
(= (stored-it) (let $c (collapse (superpose (f f))) (add-atom &self (stored $c))))
!(stored-it)
!(match &self (stored $s) $s)

; A tuple of values evaluates to itself.
(n 1)
(n 1)
(= (numbers) (let $c (collapse (match &self (n $x) $x)) (Got $c)))
!(numbers)

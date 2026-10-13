; An answer built in a binding image of its own joins the consumer's frame:
; the image names the callee's variables it saw by a schema, the consumer's
; frame numbers its slots by position, and their union holds both. Rows with
; a variable shared between fields bind two query variables to one value, so
; the second leg's answer reaches the first leg's variables only through it.
(r $u $u)
(s 1 one)
(s 2 two)
(= (nested) (match &self (r $x $n) (match &self (s $n $y) ($x $y))))
(= (join) (collapse (match &self (, (r $x $n) (s $n $y)) ($x $y))))
!(nested)
!(join)
!(match &self (r $x $n) (match &self (s $n $y) ($x $y)))
; A space small enough to be scanned row by row.
!(bind! &k (new-space))
!(add-atom &k (r $u $u))
!(add-atom &k (s 1 one))
(= (nested-k) (match &k (r $x $n) (match &k (s $n $y) ($x $y))))
(= (join-k) (collapse (match &k (, (r $x $n) (s $n $y)) ($x $y))))
!(nested-k)
!(join-k)
; A larger space, read by its indexes.
!(bind! &w (new-space))
!(add-atom &w (r $u $u))
!(add-atom &w (r c 2))
!(add-atom &w (s 1 one))
!(add-atom &w (s 2 two))
!(let $i (superpose (1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24)) (add-atom &w (pad $i)))
(= (nested-w) (collapse (match &w (r $x $n) (match &w (s $n $y) ($x $y)))))
(= (join-w) (collapse (match &w (, (r $x $n) (s $n $y)) ($x $y))))
!(nested-w)
!(join-w)
; Rows read through an imported module.
!(import! &self ./support/frame_schema_facts.metta)
(= (nested-m) (collapse (match &self (mr $x $n) (match &self (ms $n $y) ($x $y)))))
(= (join-m) (collapse (match &self (, (mr $x $n) (ms $n $y)) ($x $y))))
!(nested-m)
!(join-m)
; Negative cases: the shared variable still binds both fields to one value.
(= (mismatch) (collapse (match &self (r $x $n) (match &self (s $n $y) (if (== $x $n) ok bad)))))
!(mismatch)
!(collapse (match &k (r 1 2) found))
!(collapse (match &w (, (r $x $n) (s $n three)) $x))

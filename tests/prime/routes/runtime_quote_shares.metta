; A quotation built while the program runs, around a store variable still
; unbound, shares it: the answer does not depend on whether the variable is
; bound before or after the quotation is built.  Sealing it when the
; quotation is built would give the bound-after rows (quote $x) and the
; bound-before rows (quote 5).  A quotation written in the program is sealed
; when it is read: a name a pattern of the form binds is its own there.

; ---- Built by cons-atom, by union-atom, and by lift, in both orders.
!(row cons-atom-bound-after)
!(let $q (cons-atom quote ($x)) (let $x 5 $q))
!(row cons-atom-bound-before)
!(let $x 5 (cons-atom quote ($x)))
!(row union-atom-bound-after)
!(let $q (union-atom (quote) ($x)) (let $x 5 $q))
!(row union-atom-bound-before)
!(let $x 5 (union-atom (quote) ($x)))
!(row lift-bound-after)
!(let $q (lift $x) (let $x 5 $q))
!(row lift-bound-before)
!(let $x 5 (lift $x))

; ---- Written quotations are sealed, in both orders.
!(row written-bound-after)
!(let $q (quote $x) (let $x 5 $q))
!(row written-bound-before)
!(let $x 5 (quote $x))

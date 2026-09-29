; A match over a small space on the tier reads the space's rows once per
; revision and tests each row's root before the row: rows with variables,
; a row headed by a variable, rows of other lengths and other heads,
; patterns headed by a variable or a variable altogether, spaces changed
; between matches, and two spaces read in turn.

!(bind! &a (new-space))
!(bind! &b (new-space))
!(add-atom &a (edge 1 2))
!(add-atom &a (edge 2 3))
!(add-atom &a (edge $x $x))
!(add-atom &a ($rel 9 9))
!(add-atom &a (edge 7))
!(add-atom &a (node 1 2))
!(add-atom &b (edge 5 6))

(= (targets $s $a) (msort (collapse (match $s (edge $a $b) $b))))
(= (via $s $v) (msort (collapse (match $s ($r $v $w) (seen $r $w)))))
(= (rows $s) (size-atom (collapse (match $s $row $row))))
(= (in-turn $a) (both (targets &a $a) (targets &b $a)))

!(targets &a 1)
!(targets &a 9)
!(targets &b 5)
!(via &a 2)
!(rows &a)
!(rows &b)
!(in-turn 5)
!(in-turn 2)
!(add-atom &a (edge 1 4))
!(targets &a 1)
!(remove-atom &a (edge 1 2))
!(targets &a 1)
!(add-atom &b (edge 2 8))
!(in-turn 2)
!(rows &b)

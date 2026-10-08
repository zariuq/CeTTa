; The two demand bubbles give the same answers on every route: committed to
; eagerness, a discarded coin contributes both its answers; licensed, it is
; left lazy; a binding of the coin used twice gives two pairs shared and
; four resampled.
(= coin heads)
(= coin tails)
(= (k $x $y) $x)
!(k 1 coin)
!(demand:committed eager (k 1 coin))
!(demand:licensed eager (k 1 coin))
!(let $c coin (P $c $c))
!(demand:committed resample (let $c coin (P $c $c)){$c})
!(demand:licensed resample (let $c coin (P $c $c)){$c})

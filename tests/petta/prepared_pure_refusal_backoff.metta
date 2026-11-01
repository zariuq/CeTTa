; pick-if's untaken branch evaluates variables no parameter binds, so the
; prepared machine refuses the program, for a reason that certifies nothing
; about later calls; repeat-if calls it 2,000 times.
(= (pick-if $n) (if (> $n 0) $n (Unused $local0 $local1 $local2 $local3 $local4 $local5 $local6 $local7)))
(= (repeat-if $n $acc)
   (if (> $n 0) (repeat-if (- $n 1) (+ $acc (pick-if 1))) $acc))
!(repeat-if 2000 0)

; Only the selected branch needs a materialized value.
(= (contextual-if $n)
   (if (> $n 0) $n (Unused $a $b $c $d)))
!(contextual-if 1)

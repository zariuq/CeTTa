; J2: J1's non-linear head, with the goal growing by instantiation across
; recursive calls.  Size ladder: change the grow count (512, 2048, 8192).
(= (pair-eq (P $x $x)) ok)
(= (grow $n $t)
   (if (== $n 0)
       (pair-eq (P $t $t))
       (grow (- $n 1) (node $n $t))))
!(grow 8192 leaf)

; J1: open goal against a non-linear open head.
; The pattern repeats $x; two independently built spines are unified for
; consistency.  Size ladder: change the spine length (512, 2048, 8192).
(= (pair-eq (P $x $x)) ok)
(= (spine $n)
   (if (== $n 0)
       leaf
       (let $t (spine (- $n 1))
         (node $n $t))))
(= (j1-run $n $l $r)
   (if (== $n 0)
       (pair-eq (P $l $r))
       (let $_ (pair-eq (P $l $r))
         (j1-run (- $n 1) $l $r))))
!(let* (($l (spine 512))
        ($r (spine 512)))
   (j1-run 256 $l $r))

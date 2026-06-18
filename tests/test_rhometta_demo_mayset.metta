!(import! &self rhometta)
!(import! &self lib_pln)

(link (Inheritance A B1) (stv 0.8 0.9))
(link (Inheritance B1 C) (stv 0.7 0.8))
(link (Inheritance A B2) (stv 0.6 0.9))
(link (Inheritance B2 C) (stv 0.9 0.7))
(link (Inheritance D E1) (stv 0.9 0.9))
(link (Inheritance E1 F) (stv 0.8 0.8))
(link (Inheritance D E2) (stv 0.7 0.9))
(link (Inheritance E2 F) (stv 0.6 0.8))
(link (Inheritance D E3) (stv 0.5 0.9))
(link (Inheritance E3 F) (stv 0.4 0.8))

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (prove $a $c)
   (match &self (link (Inheritance $a $b) $t1)
     (match &self (link (Inheritance $b $c) $t2)
       (proof (Inheritance $a $c) (via $b)
              (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)
                               $t1 $t2)))))

(= (one-ch)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (prove A C)))
     (rho:recv (chan 1) $y (rho:drop $y))))

(= (two-ch)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (prove A C)))
     (rho:recv (chan 1) $y (rho:drop $y))
     (rho:send (chan 2) (rhometta:eval (prove D F)))
     (rho:recv (chan 2) $z (rho:drop $z))))

; May-set faithfulness: every derivation of the payload is a distinct outcome.
!(assertEqual
  (collapse (rhometta:run (one-ch)))
  (collapse (rho:val (prove A C))))

; 2 proofs x 3 proofs = 6 quiescent outcomes; nothing dropped, nothing invented.
!(assertEqual (TupleCount (collapse (rhometta:run (two-ch)))) 6)

; One-step frontier exposes exactly the derivations.
!(assertEqual (TupleCount (collapse (rhometta:transitions (one-ch)))) 2)

; Threaded re-run preserves the may-set.
!(pragma! num-threads 2)
!(assertEqual
  (collapse (rhometta:run (one-ch)))
  (collapse (rho:val (prove A C))))
!(assertEqual (TupleCount (collapse (rhometta:run (two-ch)))) 6)

; rhometta proof-search may-set demo.
; Multi-result deferred payloads = one COMM outcome per derivation;
; (rhometta:run P) returns ALL quiescent residuals (the may-set).

!(import! &self rhometta)
!(import! &self lib_pln)

; Knowledge base: two-hop Inheritance chains, link truth values.
; A -> C has 2 proof paths (via B1, via B2).
(link (Inheritance A B1) (stv 0.8 0.9))
(link (Inheritance B1 C) (stv 0.7 0.8))
(link (Inheritance A B2) (stv 0.6 0.9))
(link (Inheritance B2 C) (stv 0.9 0.7))
; D -> F has 3 proof paths (via E1, E2, E3).
(link (Inheritance D E1) (stv 0.9 0.9))
(link (Inheritance E1 F) (stv 0.8 0.8))
(link (Inheritance D E2) (stv 0.7 0.9))
(link (Inheritance E2 F) (stv 0.6 0.8))
(link (Inheritance D E3) (stv 0.5 0.9))
(link (Inheritance E3 F) (stv 0.4 0.8))

; Channels are quoted processes (strict rho: names are @P).  A single
; if-guarded equation keeps minting deterministic: two equations with an
; overlapping (chan 0) pattern would fork a garbage (chan -1) branch.
(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; One PLN deduction proof per derivation path; node TVs are fixed at
; (stv 0.5 0.9) so Truth_Deduction's consistency precondition always holds.
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

!("one-ch: all proofs of A -> C (expect 2 outcomes, via B1 and via B2)")
!(collapse (rhometta:run (one-ch)))

!("two-ch: proofs of A -> C x proofs of D -> F (expect 2 x 3 = 6 outcomes)")
!(TupleCount (collapse (rhometta:run (two-ch))))

!("one-step frontier of one-ch (expect 2 successors, one per derivation)")
!(TupleCount (collapse (rhometta:transitions (one-ch))))

; Crossing the large-equation-bucket threshold must not change the meaning of
; an open query.  The extra clauses are deliberately irrelevant to the query;
; they only make the substitution-tree index eligible.

(= (kb) (superpose ((: w1 fact) (: w2 (imp fact win)))))
(= (rb) (: MP (-> (imp $x $y) $x $y)))

(= (synth-small $query $kb $rb $depth)
   (let* (((: $proof $premise) ($kb))
          ((: $proof $premise) $query))
     $query))
(= (synth-small $query $kb $rb (S $k))
   (let* (((: $ructor (-> $p1 $p2 $c)) ($rb))
          ((: ($ructor $pf1 $pf2) $c) $query)
          ((: $pf1 $p1) (synth-small (: $pf1 $p1) $kb $rb $k))
          ((: $pf2 $p2) (synth-small (: $pf2 $p2) $kb $rb $k)))
     $query))

(= (synth-large $query $kb $rb $depth)
   (let* (((: $proof $premise) ($kb))
          ((: $proof $premise) $query))
     $query))
(= (synth-large $query $kb $rb (S $k))
   (let* (((: $ructor (-> $p1 $p2 $c)) ($rb))
          ((: ($ructor $pf1 $pf2) $c) $query)
          ((: $pf1 $p1) (synth-large (: $pf1 $p1) $kb $rb $k))
          ((: $pf2 $p2) (synth-large (: $pf2 $p2) $kb $rb $k)))
     $query))
(= (synth-large extra1) nope1)
(= (synth-large extra2) nope2)
(= (synth-large extra3) nope3)

!(synth-small (: $proof win) kb rb (S Z))
!(synth-large (: $proof win) kb rb (S Z))
; A genuinely absent conclusion remains absent in both indexed and legacy
; execution.  This command intentionally emits no answer.
!(synth-large (: $proof lost) kb rb (S Z))

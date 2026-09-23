; A language operation over an admitted presentation whose rules are data.
; (lang:step <authority> <goal>) matches the goal against every rule's
; conclusion and returns, for each applicable rule, the instantiated formals
; and premises; a formal the conclusion does not determine stays as a named
; unknown.  A bounded backward deriver written in MeTTa then chooses steps,
; builds the certificate from them, and the admitted checker accepts it:
; the derivation and the certificate come from the same rule data.
; whose four rules are data: hypothesis, weakening, introduction, elimination.
(= (matom $n) (let $v $n (PApp "MAtom" (LCons $v LNil))))
(= (mzero) (PApp "MZero" LNil))
(= (msucc $n) (let $v $n (PApp "MSucc" (LCons $v LNil))))
(= (mimp $a $b) (let $x $a (let $y $b (PApp "MImp" (LCons $x (LCons $y LNil))))))
(= (mnil) (PApp "MCtxNil" LNil))
(= (mcons $a $ctx) (let $x $a (let $c $ctx (PApp "MCtxCons" (LCons $x (LCons $c LNil))))))
(= (mproves $ctx $f) (let $c $ctx (let $x $f (PApp "MProves" (LCons $c (LCons $x LNil))))))
(= (rule $name $args $children) (let $as $args (let $ch $children (GProof (GRuleInst $name $as) $ch))))
(= (args2 $a $b) (let $x $a (let $y $b (LCons $x (LCons $y LNil)))))
(= (args3 $a $b $c) (let $x $a (let $y $b (let $z $c (LCons $x (LCons $y (LCons $z LNil)))))))
(= (kids1 $a) (let $x $a (PrCons $x PrNil)))
(= (kids2 $a $b) (let $x $a (let $y $b (PrCons $x (PrCons $y PrNil)))))
(= (hyp-zero $ctx $f) (rule "megalodon-hyp-zero" (args2 $ctx $f) PrNil))
(= (weaken $ctx $f $head $d) (rule "megalodon-weaken" (args3 $ctx $f $head) (kids1 $d)))
(= (imp-intro $ctx $a $b $d) (rule "megalodon-imp-intro" (args3 $ctx $a $b) (kids1 $d)))
(= (imp-elim $ctx $a $b $d1 $d2) (rule "megalodon-imp-elim" (args3 $ctx $a $b) (kids2 $d1 $d2)))
(= (P) (matom (mzero)))
(= (Q) (matom (msucc (mzero))))
(= (A) (mimp (P) (Q)))

; ---- steps as data
(= (step-names (GStepsV1 LNil)) ())
(= (step-names (GStepsV1 (LCons (GStepV1 $name $args $premises) $rest)))
   (let $more (step-names (GStepsV1 $rest)) (cons-atom $name $more)))
(= (premise-count LNil) 0)
(= (premise-count (LCons $p $rest)) (let $n (premise-count $rest) (+ $n 1)))
(= (step-shapes (GStepsV1 LNil)) ())
(= (step-shapes (GStepsV1 (LCons (GStepV1 $name $args $premises) $rest)))
   (let $k (premise-count $premises) (let $more (step-shapes (GStepsV1 $rest)) (cons-atom ($name $k) $more))))
; the closed goal |- P -> P: only introduction applies
!(let $g (mproves (mnil) (mimp (P) (P))) (let $s (lang:step MEGALODON-IMP $g) (step-shapes $s)))
; the hypothesis goal [P] |- P: the hypothesis rule, weakening and elimination apply
!(let $g (mproves (mcons (P) (mnil)) (P)) (let $s (lang:step MEGALODON-IMP $g) (step-shapes $s)))
; a goal no rule concludes, and an unknown language
!(lang:step DTT (foo))
!(lang:step nosuch (foo))

; ---- a bounded backward deriver over the steps, and the checker's verdict
; on the certificate it builds.  Steps whose premises still contain an
; unknown are skipped; the first fully determined derivation is returned.
(= (has-unknown $t)
   (if (== (get-metatype $t) Expression)
       (if (unify $t (FVar $_) True False) True (let $flags (map-atom $t $e (has-unknown $e)) (any-true $flags)))
       False))
(= (any-true $flags)
   (if (== $flags ()) False
       (let $h (car-atom $flags) (if $h True (let $t (cdr-atom $flags) (any-true $t))))))
(= (derive $goal 0) none)
(= (derive $goal $depth)
   (let $steps (lang:step MEGALODON-IMP $goal)
     (let $d (- $depth 1)
       (first-derivation $steps $d))))
(= (first-derivation (GStepsV1 LNil) $d) none)
(= (first-derivation (GStepsV1 (LCons (GStepV1 $name $args $premises) $rest)) $d)
   (let $u (has-unknown $premises)
     (if $u
         (first-derivation (GStepsV1 $rest) $d)
         (let $kids (derive-all $premises $d)
           (if (== $kids none)
               (first-derivation (GStepsV1 $rest) $d)
               (GProof (GRuleInst $name $args) $kids))))))
(= (derive-all LNil $d) PrNil)
(= (derive-all (LCons $p $rest) $d)
   (let $one (derive $p $d)
     (if (== $one none) none
         (let $more (derive-all $rest $d)
           (if (== $more none) none (PrCons $one $more))))))
; the rule names of a certificate, in derivation order
(= (cert-rules (GProof (GRuleInst $name $args) $kids)) (let $more (kids-rules $kids) (cons-atom $name $more)))
(= (kids-rules PrNil) ())
(= (kids-rules (PrCons $k $rest)) (let $a (cert-rules $k) (let $b (kids-rules $rest) (append-list $a $b))))
(= (append-list $a $b)
   (if (== $a ()) $b
       (let $h (car-atom $a) (let $t (cdr-atom $a) (let $r (append-list $t $b) (cons-atom $h $r))))))
; |- P -> P, derived from the rules and accepted by the checker
!(let $g (mproves (mnil) (mimp (P) (P)))
   (let $cert (derive $g 4)
     (let $names (cert-rules $cert) (Pair $names (nik:check MEGALODON-IMP $g $cert)))))
; |- (P -> Q) -> (P -> Q) as well; the deriver never invents a choice, so a
; goal that needs elimination with an undetermined domain is not derived
!(let $g (mproves (mnil) (mimp (A) (A)))
   (let $cert (derive $g 4) (let $names (cert-rules $cert) (Pair $names (nik:check MEGALODON-IMP $g $cert)))))
!(let $g (mproves (mcons (A) (mcons (P) (mnil))) (Q)) (derive $g 4))

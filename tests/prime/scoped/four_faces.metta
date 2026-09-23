; One theorem, four faces.  Implication transitivity,
;   forall P Q R. (P -> Q) -> (Q -> R) -> (P -> R),
; is stated and checked as HOL in the set: language, as a dependent type in
; the type: language, as a NIK certificate against the admitted implication
; calculus, and as a Boolean model computation.  Two distinct negatives are
; kept apart on every face that can express them: an invalid proof of the
; true theorem, which a checker rejects without saying anything about the
; theorem, and the false variant with the last two antecedents swapped,
;   forall P Q R. (P -> Q) -> (Q -> R) -> (R -> P),
; which only the model face refutes as a proposition; the proof faces can
; only fail to prove it.
!(import! &self pf)
!(bind! &thy (new-space))

; ---- HOL face: a natural-deduction proof over simple types
!(pf:theorem &thy imp-trans
   (all prop (lam P (all prop (lam Q (all prop (lam R (imp (imp P Q) (imp (imp Q R) (imp P R)))))))))
   (pf:fix P (pf:fix Q (pf:fix R (pf:assume f (pf:assume g (pf:assume x (pf:by g (pf:by f x)))))))))
; an invalid proof of the true theorem: the applications in the wrong order
!(set:proves &thy
   (pf:fix P (pf:fix Q (pf:fix R (pf:assume f (pf:assume g (pf:assume x (pf:by f (pf:by g x))))))))
   (all prop (lam P (all prop (lam Q (all prop (lam R (imp (imp P Q) (imp (imp Q R) (imp P R))))))))))
; the false variant: the valid proof does not prove it, which is all a checker can say
!(set:proves &thy
   (pf:fix P (pf:fix Q (pf:fix R (pf:assume f (pf:assume g (pf:assume x (pf:by g (pf:by f x))))))))
   (all prop (lam P (all prop (lam Q (all prop (lam R (imp (imp P Q) (imp (imp Q R) (imp R P))))))))))

; ---- DTT face: propositions as types, the proof is function composition
!(type:check (lam P (lam Q (lam R (lam f (lam g (lam x (g (f x))))))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (R : (u 0)) (-> (f : (-> P Q)) (-> (g : (-> Q R)) (-> (x : P) R)))))))
; an invalid inhabitant of the true type: the wrong composition
!(type:check (lam P (lam Q (lam R (lam f (lam g (lam x (f (g x))))))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (R : (u 0)) (-> (f : (-> P Q)) (-> (g : (-> Q R)) (-> (x : P) R)))))))
; the false variant as a type: the valid term does not inhabit it
!(type:check (lam P (lam Q (lam R (lam f (lam g (lam x (g (f x))))))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (R : (u 0)) (-> (f : (-> P Q)) (-> (g : (-> Q R)) (-> (x : R) P)))))))
; the two faces agree on conjunction introduction as well: Church pair and dependent pair
!(set:define &thy and (-> prop prop prop) (= (and $p $q) (all prop (lam r (imp (imp $p (imp $q r)) r)))))
!(pf:theorem &thy and-intro (all prop (lam P (all prop (lam Q (imp P (imp Q (and P Q)))))))
   (pf:fix P (pf:fix Q (pf:assume p (pf:assume q (pf:fix r (pf:assume k (pf:by k p q))))))))
!(type:check (lam P (lam Q (lam p (lam q (pair p q)))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (p : P) (-> (q : Q) (sigma (x : P) Q))))))
!(type:check (lam P (lam Q (lam p (lam q (pair q p)))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (p : P) (-> (q : Q) (sigma (x : P) Q))))))
; and on conjunction elimination: the HOL proof selects a component, the DTT term projects it
!(pf:theorem &thy and-left (all prop (lam P (all prop (lam Q (imp (and P Q) P)))))
   (pf:fix P (pf:fix Q (pf:assume h (pf:by h (pf:assume p (pf:assume q p)))))))
!(type:check (lam P (lam Q (lam h (fst h))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (h : (sigma (x : P) Q)) P))))
!(type:check (lam P (lam Q (lam h (snd h))))
   (-> (P : (u 0)) (-> (Q : (u 0)) (-> (h : (sigma (x : P) Q)) P))))

; ---- model face: the Boolean model, every valuation, by computation
(= (impb True $q) $q)
(= (impb False $q) True)
(= (truth-values $f) (let $p (superpose (True False)) (let $q (superpose (True False)) (let $r (superpose (True False)) ($f $p $q $r)))))
(= (imp-trans-b $p $q $r) (impb (impb $p $q) (impb (impb $q $r) (impb $p $r))))
(= (imp-wrong-b $p $q $r) (impb (impb $p $q) (impb (impb $q $r) (impb $r $p))))
; valid: only True appears; the wrong variant has a falsifying valuation
!(collapse (unique (truth-values imp-trans-b)))
!(collapse (unique (truth-values imp-wrong-b)))

; ---- NIK face: a certificate against the admitted implication calculus,
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
; P = atom 0, Q = atom 1, R = atom 2; A = P -> Q, B = Q -> R
(= (P) (matom (mzero)))
(= (Q) (matom (msucc (mzero))))
(= (R) (matom (msucc (msucc (mzero)))))
(= (A) (mimp (P) (Q)))
(= (B) (mimp (Q) (R)))
(= (imp-trans-goal) (mproves (mnil) (mimp (A) (mimp (B) (mimp (P) (R))))))
(= (imp-wrong-goal) (mproves (mnil) (mimp (A) (mimp (B) (mimp (R) (P))))))
(= (imp-trans-certificate)
   (let $c1 (mcons (A) (mnil))
   (let $c2 (mcons (B) $c1)
   (let $c3 (mcons (P) $c2)
     (imp-intro (mnil) (A) (mimp (B) (mimp (P) (R)))
       (imp-intro $c1 (B) (mimp (P) (R))
         (imp-intro $c2 (P) (R)
           (imp-elim $c3 (Q) (R)
             (weaken $c2 (B) (P) (hyp-zero $c1 (B)))
             (imp-elim $c3 (P) (Q)
               (weaken $c2 (A) (P) (weaken $c1 (A) (B) (hyp-zero (mnil) (A))))
               (hyp-zero $c2 (P)))))))))))
!(let $g (imp-trans-goal) (let $c (imp-trans-certificate) (nik:check MEGALODON-IMP $g $c)))
; an invalid certificate for the true goal: the two elimination children swapped
(= (imp-trans-bad-certificate)
   (let $c1 (mcons (A) (mnil))
   (let $c2 (mcons (B) $c1)
   (let $c3 (mcons (P) $c2)
     (imp-intro (mnil) (A) (mimp (B) (mimp (P) (R)))
       (imp-intro $c1 (B) (mimp (P) (R))
         (imp-intro $c2 (P) (R)
           (imp-elim $c3 (Q) (R)
             (imp-elim $c3 (P) (Q)
               (weaken $c2 (A) (P) (weaken $c1 (A) (B) (hyp-zero (mnil) (A))))
               (hyp-zero $c2 (P)))
             (weaken $c2 (B) (P) (hyp-zero $c1 (B)))))))))))
!(let $g (imp-trans-goal) (let $c (imp-trans-bad-certificate) (nik:check MEGALODON-IMP $g $c)))
; the false variant as a goal: the valid certificate does not certify it
!(let $g (imp-wrong-goal) (let $c (imp-trans-certificate) (nik:check MEGALODON-IMP $g $c)))

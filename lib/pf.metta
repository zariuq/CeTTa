; Proof verbs for the set: proof language.
;
; The checker owns names, hypotheses and lemma instantiation:
;   (pf:fix x body)        (pf:assume h body)        (pf:have h proof body)
;   (pf:by name p1 ... pk) a known fact or hypothesis, instantiated by
;                          matching its conclusion against the goal
; The verbs below are ordinary programs that return core proof terms;
; nothing they produce is believed until the checker has seen it.  The
; equational verbs expand to the two equality axioms of a carrier, refl and
; substitution, so a theory needs no congruence, symmetry or transitivity
; axioms.
;
;   (pf:theory &t (decl ...))          declare the object theory in a space
;   (pf:equality &t A refl subst)      publish refl and substitution for A
;   (pf:refl A a)                      eq A a a
;   (pf:sym A a b p)                   eq A b a from p : eq A a b
;   (pf:trans A a b c p q)             eq A a c from p, q
;   (pf:cong A B F a b p)              eq B (F a) (F b) from p : eq A a b
;   (pf:calc A t0 (= t1 j1) ...)       an equational chain in carrier A;
;      a justification j is a proof term, (by name p ...), (sym j) when j
;      proves the step reversed, or (cong A' F a b p) for the step
;      F a = F b in carrier A
;   (pf:induction principle motive base step)

(= (pf:theory $space $decls) (let $added (map-atom $decls $d (add-atom $space $d)) ()))

; publish a theorem, or check a proof, through the judgments
; The verbs in a proof are expanded to core terms bottom-up before the
; judgment reads the proof as data.  Expansion is explicit so that it does
; not depend on the evaluator's order: children first, then the node if its
; head is a verb, then the verb's output again.
(= (pf:verbs) (pf:calc-from pf:calc-chain pf:step pf:cong pf:sym pf:trans
               pf:refl pf:subst-with pf:apply pf:at pf:induction-from))
; Values are forced with let before they are compared, deconstructed or
; passed on: an atom builtin in argument position reaches its callee as
; syntax, not as its value.
(= (pf:member $x $list)
   (if (== $list ()) False
       (let $h (car-atom $list)
         (if (== $h $x) True
             (let $rest (cdr-atom $list) (pf:member $x $rest))))))
(= (pf:verb-head $t)
   (let $h (car-atom $t)
     (if (== (get-metatype $h) Symbol)
         (let $verbs (pf:verbs) (pf:member $h $verbs))
         False)))

(= (pf:expand-list $t)
   (if (== $t ()) ()
       (let $h (car-atom $t)
         (let $rest (cdr-atom $t)
           (let $eh (pf:expand $h)
             (let $et (pf:expand-list $rest)
               (cons-atom $eh $et)))))))

(= (pf:expand $t)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) ()
           (let $node (pf:expand-list $t)
             (let $head (car-atom $node)
               (if (== $head pf:induction)
                   (let $P (index-atom $node 1)
                   (let $M (index-atom $node 2)
                   (let $r1 (cdr-atom $node)
                   (let $r2 (cdr-atom $r1)
                   (let $cases (cdr-atom $r2)
                   (let $out (pf:induction-from (pf:all-elim (pf:known $P) $M) $cases)
                     (pf:expand $out)))))))
               (if (== $head pf:calc)
                   (let $A (index-atom $node 1)
                   (let $t0 (index-atom $node 2)
                   (let $r1 (cdr-atom $node)
                   (let $r2 (cdr-atom $r1)
                   (let $steps (cdr-atom $r2)
                   (let $out (pf:calc-from $A $t0 $steps)
                     (pf:expand $out)))))))
                   (if (pf:verb-head $node)
                       (let $out $node (if (== $out $node) $node (pf:expand $out)))
                       $node))))))
       $t))

; Syntax is handed to a judgment quoted: the evaluator then leaves it
; alone even where a constant shares its name with a builtin.
(: pf:theorem (-> Atom Atom Atom Atom %Undefined%))
(= (pf:theorem $space $name $prop $proof)
   (let $p (pf:expand $proof)
     (let $qp (cons-atom quote ($p))
       (let $qprop (cons-atom quote ($prop))
         (set:theorem $space $name $qprop $qp)))))
(: pf:proves (-> Atom Atom Atom %Undefined%))
(= (pf:proves $space $proof $prop)
   (let $p (pf:expand $proof)
     (let $qp (cons-atom quote ($p))
       (let $qprop (cons-atom quote ($prop))
         (set:proves $space $qp $qprop)))))

(= (pf:equality $space $A $refl $subst)
   (let $r (set:axiom $space $refl (all $A (lam a (eq $A a a))))
     (let $s (set:axiom $space $subst
               (all (-> $A prop) (lam P (all $A (lam a (all $A (lam b
                 (imp (eq $A a b) (imp (P a) (P b))))))))))
       (let $t (add-atom &self (pf:equality-of $A $refl $subst))
         ($r $s)))))
; the carrier is compared after the match, so that an arrow carrier is
; never placed inside a query pattern
(= (pf:at refl $A) (let $rows (collapse (match &self (pf:equality-of $c $r $s) ($c $r))) (pf:find-carrier $A $rows)))
(= (pf:at subst $A) (let $rows (collapse (match &self (pf:equality-of $c $r $s) ($c $s))) (pf:find-carrier $A $rows)))
(= (pf:find-carrier $A $rows)
   (if (== $rows ()) (pf:no-equality-for $A)
       (let ($c $name) (car-atom $rows)
         (if (== $c $A) $name
             (let $rest (cdr-atom $rows) (pf:find-carrier $A $rest))))))

; substitute the symbol s by r in t, respecting lam binders of the same name
(= (pf:subst-list $s $r $t)
   (if (== $t ()) ()
       (let $h (car-atom $t)
         (let $rest (cdr-atom $t)
           (let $sh (pf:subst $s $r $h)
             (let $st (pf:subst-list $s $r $rest)
               (cons-atom $sh $st)))))))

(= (pf:subst $s $r $t)
   (if (== $t $s) $r
       (if (== (get-metatype $t) Expression)
           (if (unify $t (lam $s $_) True False)
               $t
               (pf:subst-list $s $r $t))
           $t)))

; apply a context: a lambda is reduced, anything else is applied
(= (pf:apply $F $a)
   (if (unify $F (lam $y $body) True False)
       (unify $F (lam $y $body) (pf:subst $y $a $body) none)
       ($F $a)))

(= (pf:refl $A $a) (pf:all-elim (pf:known (pf:at refl $A)) $a))

; subst@A at P a b, applied to p : eq A a b, then to q : P a
(= (pf:subst-with $A $P $a $b $p $q)
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known (pf:at subst $A)) $P) $a) $b)
       $p)
     $q))

(= (pf:sym $A $a $b $p)
   (pf:subst-with $A (lam pf-z (eq $A pf-z $a)) $a $b $p (pf:refl $A $a)))

(= (pf:trans $A $a $b $c $p $q)
   (pf:subst-with $A (lam pf-z (eq $A $a pf-z)) $b $c $q $p))

(= (pf:cong $A $B $F $a $b $p)
   (let $Fa (pf:apply $F $a)
     (pf:subst-with $A (lam pf-z (eq $B $Fa (pf:apply $F pf-z))) $a $b $p
                    (pf:refl $B $Fa))))

; ---- calc ----------------------------------------------------------------

(= (pf:step $A $from $to $j)
   (if (unify $j (sym $k) True False)
       (unify $j (sym $k) (pf:sym $A $to $from (pf:step $A $to $from $k)) none)
       (if (unify $j (cong $A2 $F $a $b $p) True False)
           (unify $j (cong $A2 $F $a $b $p) (pf:cong $A2 $A $F $a $b $p) none)
           (if (unify $j (by $name) True False)
               (unify $j (by $name) (pf:by $name) none)
               (if (unify $j (by $name $p1) True False)
                   (unify $j (by $name $p1) (pf:by $name $p1) none)
                   (if (unify $j (by $name $p1 $p2) True False)
                       (unify $j (by $name $p1 $p2) (pf:by $name $p1 $p2) none)
                       $j))))))

; (pf:calc A t0 step ...) takes any number of steps; the expander hands the
; steps to pf:calc-from as one list.

; the first step, then a chain of transitivity steps
(= (pf:calc-from $A $t0 $steps)
   (let (= $t1 $j1) (car-atom $steps)
     (let $p1 (pf:step $A $t0 $t1 $j1)
       (let $rest (cdr-atom $steps)
         (pf:calc-chain $A $t0 $t1 $p1 $rest)))))

(= (pf:calc-chain $A $t0 $tk $pk $steps)
   (if (== $steps ()) $pk
       (let (= $tn $jn) (car-atom $steps)
         (let $pn (pf:step $A $tk $tn $jn)
           (let $pkn (pf:trans $A $t0 $tk $tn $pk $pn)
             (let $rest (cdr-atom $steps)
               (pf:calc-chain $A $t0 $tn $pkn $rest)))))))

; ---- induction -----------------------------------------------------------

; (pf:induction principle motive case ...) takes one case per constructor;
; the expander folds the cases onto the instantiated principle.
(= (pf:induction-from $proof $cases)
   (if (== $cases ()) $proof
       (let $c (car-atom $cases)
         (let $rest (cdr-atom $cases)
           (pf:induction-from (pf:imp-elim $proof $c) $rest)))))

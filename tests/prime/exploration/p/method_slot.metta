(set:profile hol)

; Proof plans as partial programs: generalizing two plans leaves a method slot.
;
; The kernel is a small proof calculus with nullary judgments; each rule is
; (rule name premises conclusion):
;   axA1, axA2 : |- A      ab : A |- B       axB : |- B
;   bc : B |- C            xc : X |- C       pair : A, A |- D
; A derivation is a rule applied to derivations of its premises; a nullary
; rule is written by its name.  So bc(ab(axA1)) and bc(axB) are two
; derivations of C, and pair(axA1, axA2) and pair(axA2, axA1) two of D.
;
; A plan is (plan obligations body): an ordered list of judgments still to be
; established, and a derivation of the goal from them.  The draft has no hole
; former, so a hole is an undeclared head applied to what it depends on, as in
; the round-2 hole fixtures: (? i) is obligation i of the plan.  The head ?
; has no equation, so a hole waits.  A plan's holes are its only free parts:
; filling hole i with evidence for obligation i is completing the plan
; (discharge), and a closed derivation completes a plan when some typed
; evidence fills it to exactly that derivation.  Every plan here cites each of
; its obligations.
;
; Anti-unification of two plans for one goal keeps the root rule where they
; apply the same rule and leaves a method slot, a new obligation for the
; judgment at that position, where they differ.  It returns the generalization
; and the two substitutions that recover the originals.  It is linear: every
; disagreement gets its own slot, and slots are never merged.
;
; The ITP half reads a plan with obligations G1 ... Gn and goal g as a proof of
; G1 -> ... -> Gn -> g, built from the plan by turning hole i into hypothesis
; i; completion is application to proofs of the obligations.  The proof layer
; checks proofs and does not compare them, so the loss of an original under
; merged slots is visible only in the program.
;
; Mirrors: methodSlot, antiUnify_left, antiUnify_right,
; antiUnify_instance_left, antiUnify_instance_right, antiUnify_refines_left,
; antiUnify_refines_right, generalization_is_pair, merged_slots_lose_original,
; antiUnify_self_obligations, mem_completions_iff_discharge, wire_positive,
; wire_wrong_evidence_rejected, Truth.plan_sound.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- the kernel ------------------------------------------------------------------

(rule axA1 () A)
(rule axA2 () A)
(rule ab (A) B)
(rule axB () B)
(rule bc (B) C)
(rule xc (X) C)
(rule pair (A A) D)

(= (rule-shape $r)
   (let $found (collapse (match &self (rule $r $ps $c) ($ps $c)){$r})
     (if (== $found ()) unknown-rule (car-atom $found))))

; ---- lists ------------------------------------------------------------------------

(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (append $t $ys))) (cons-atom $h $r)){$h $t $r}))
(= (nth $xs $i)
   (if (== $xs ()) out-of-range
       (if (== $i 0) (car-atom $xs) (let* (($t (cdr-atom $xs)) ($j (- $i 1))) (nth $t $j)){$t $j})))

; ---- derivations with holes ------------------------------------------------------

(= (is-hole $t)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) False (let $h (car-atom $t) (== $h ?)){$h})
       False))
(= (head-of $t) (if (== (get-metatype $t) Symbol) $t (car-atom $t)))
(= (kids-of $t) (if (== (get-metatype $t) Symbol) () (cdr-atom $t)))
(= (rebuild $r $kids) (if (== $kids ()) $r (cons-atom $r $kids)))

; The judgment a derivation concludes over the obligations ctx, or ill-formed.
(= (conclusion $ctx $t)
   (if (is-hole $t)
       (let $i (index-atom $t 1) (nth $ctx $i)){$i}
       (let* (($r (head-of $t)) ($kids (kids-of $t)) ($shape (rule-shape $r)))
         (if (== $shape unknown-rule) ill-formed
             (let* ((($ps $c) $shape)
                    ($got (map-atom $kids $k (conclusion $ctx $k))))
               (if (== $got $ps) $c ill-formed)){$ps $c $got})){$r $kids $shape}))

; Filling holes with evidence: discharge.
(= (fill $t $ev)
   (if (is-hole $t)
       (let $i (index-atom $t 1) (nth $ev $i)){$i}
       (let* (($r (head-of $t)) ($kids (kids-of $t))
              ($filled (map-atom $kids $k (fill $k $ev))))
         (rebuild $r $filled)){$r $kids $filled}))

; Matching a closed derivation against a plan body: the evidence each hole
; must receive, or no-match.
(= (bind-hole $i $d $env)
   (if (== $env ()) (($i $d))
       (let* ((($j $e) (car-atom $env)) ($rest (cdr-atom $env)))
         (if (== $j $i)
             (if (== $e $d) $env no-match)
             (let $env2 (bind-hole $i $d $rest)
               (if (== $env2 no-match) no-match (cons-atom ($j $e) $env2))){$env2})){$j $e $rest}))
(= (match-body $t $d $env)
   (if (== $env no-match) no-match
       (if (is-hole $t)
           (let $i (index-atom $t 1) (bind-hole $i $d $env)){$i}
           (let* (($r (head-of $t)) ($dr (head-of $d)))
             (if (== $r $dr)
                 (let* (($ks (kids-of $t)) ($ds (kids-of $d))) (match-kids $ks $ds $env)){$ks $ds}
                 no-match)){$r $dr})))
(= (match-kids $ks $ds $env)
   (if (== $ks ()) (if (== $ds ()) $env no-match)
       (if (== $ds ()) no-match
           (let* (($k (car-atom $ks)) ($d (car-atom $ds)) ($env2 (match-body $k $d $env))
                  ($kt (cdr-atom $ks)) ($dt (cdr-atom $ds)))
             (match-kids $kt $dt $env2)){$k $d $env2 $kt $dt})))
(= (lookup-evidence $env $i)
   (if (== $env ()) missing
       (let* ((($j $e) (car-atom $env)) ($rest (cdr-atom $env)))
         (if (== $j $i) $e (lookup-evidence $rest $i))){$j $e $rest}))
; Obligation i is filled with a closed derivation of its judgment.
(= (evidence-typed $ctx $i $env)
   (if (== $ctx ()) True
       (let* (($g (car-atom $ctx)) ($rest (cdr-atom $ctx)) ($e (lookup-evidence $env $i))
              ($j (+ $i 1)))
         (if (== $e missing) (uncited-obligation $i)
             (let $c (conclusion () $e)
               (if (== $c $g) (evidence-typed $rest $j $env) False)){$c})){$g $rest $e $j}))
; d completes the plan when typed evidence fills the plan to exactly d.
(= (completes $d (plan $ctx $body))
   (let $env (match-body $body $d ())
     (if (== $env no-match) False (evidence-typed $ctx 0 $env))))

(= (rule-count $t)
   (if (is-hole $t) 0
       (let $ks (kids-of $t) (foldl-atom $ks 1 $acc $k (+ $acc (rule-count $k)))){$ks}))
(= (holes $t)
   (if (is-hole $t) (let $i (index-atom $t 1) ($i)){$i}
       (let $ks (kids-of $t)
         (foldl-atom $ks () $acc $k (let $hk (holes $k) (append $acc $hk)))){$ks}))

; ---- anti-unification --------------------------------------------------------------

; (au left right goal k): (obligations body left-evidence right-evidence), the
; new slots numbered from k.  A slot leaves the whole position as one
; obligation for its judgment.
(= (slot $l $r $goal $k) (($goal) (? $k) ($l) ($r)))
(= (au $l $r $goal $k)
   (if (is-hole $l) (slot $l $r $goal $k)
       (if (is-hole $r) (slot $l $r $goal $k)
           (let* (($lh (head-of $l)) ($rh (head-of $r)))
             (if (== $lh $rh)
                 (let* ((($ps $c) (rule-shape $lh))
                        ($lk (kids-of $l)) ($rk (kids-of $r))
                        (($obls $bodies $ls $rs) (au-kids $lk $rk $ps $k))
                        ($body (rebuild $lh $bodies)))
                   ($obls $body $ls $rs)){$ps $c $lk $rk $obls $bodies $ls $rs $body}
                 (slot $l $r $goal $k))){$lh $rh})))
(= (au-kids $ls $rs $ps $k)
   (if (== $ls ()) (() () () ())
       (let* (($l (car-atom $ls)) ($r (car-atom $rs)) ($p (car-atom $ps))
              (($o1 $b1 $l1 $r1) (au $l $r $p $k))
              ($n (size-atom $o1)) ($k2 (+ $k $n))
              ($lt (cdr-atom $ls)) ($rt (cdr-atom $rs)) ($pt (cdr-atom $ps))
              (($o2 $b2 $l2 $r2) (au-kids $lt $rt $pt $k2))
              ($o (append $o1 $o2)) ($b (cons-atom $b1 $b2))
              ($lo (append $l1 $l2)) ($ro (append $r1 $r2)))
         ($o $b $lo $ro)){$l $r $p $o1 $b1 $l1 $r1 $n $k2 $lt $rt $pt $o2 $b2 $l2 $r2 $o $b $lo $ro}))
(= (generalize $l $r $goal)
   (let ($obls $body $ls $rs) (au $l $r $goal 0) ((plan $obls $body) $ls $rs)))

; ---- the method slot ------------------------------------------------------------------

(= (left-plan) (bc (ab axA1)))
(= (right-plan) (bc axB))

; 1. bc(ab(axA1)) and bc(axB) generalize to bc(?B): one obligation B and one
;    rule node, the step producing B left as a slot (methodSlot).
!(let* (($l (left-plan)) ($r (right-plan)) (($g $ls $rs) (generalize $l $r C))) $g)
!(let* (($l (left-plan)) ($r (right-plan)) (((plan $o $b) $ls $rs) (generalize $l $r C)))
   (rule-count $b))
; 2. The substitutions recover the originals (antiUnify_left, antiUnify_right).
!(let* (($l (left-plan)) ($r (right-plan)) (((plan $o $b) $ls $rs) (generalize $l $r C)))
   ((== (fill $b $ls) $l) (== (fill $b $rs) $r)))
; 3. So both originals complete the generalization: each is an instance of it
;    (antiUnify_instance_left, antiUnify_instance_right), and its completion
;    space contains theirs (antiUnify_refines_left, antiUnify_refines_right).
!(let* (($l (left-plan)) ($r (right-plan)) (($g $ls $rs) (generalize $l $r C)))
   ((completes $l $g) (completes $r $g)))
; 4. Evidence is typed: bc(ab(?A)) completed with axA1 is a derivation of C
;    (wire_positive), and axB offered for the obligation A is rejected, so
;    bc(ab(axB)) completes nothing (wire_wrong_evidence_rejected).
!(let $p (plan (A) (bc (ab (? 0))))
   ((completes (bc (ab axA1)) $p) (completes (bc (ab axB)) $p)))
!(conclusion () (bc (ab axA1)))
!(conclusion () (bc (ab axB)))

; ---- slots are never merged ---------------------------------------------------------------

; 5. pair(axA1, axA2) and pair(axA2, axA1) keep the rule pair and leave two
;    slots: obligations (A A), one rule node, holes 0 and 1
;    (generalization_is_pair).
!(let ((plan $o $b) $ls $rs) (generalize (pair axA1 axA2) (pair axA2 axA1) D)
   ($o (rule-count $b) (holes $b)))
; 6. Merging the two slots into one obligation loses an original:
;    pair(axA1, axA2) completes pair(?A0, ?A1) and not pair(?A, ?A)
;    (merged_slots_lose_original).
!((completes (pair axA1 axA2) (plan (A A) (pair (? 0) (? 1))))
  (completes (pair axA1 axA2) (plan (A) (pair (? 0) (? 0)))))
; 7. Anti-unifying pair(?A, ?A) with itself gives one slot per occurrence of
;    its obligation, two, and no other (antiUnify_self_obligations): the
;    generalization is linear.
!(let* (($p (pair (? 0) (? 0))) (((plan $o $b) $ls $rs) (generalize $p $p D)))
   ((size-atom $o) (holes $p)))

; ---- the ITP half: a plan is a proof of an implication ----------------------------------

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: A prop) (: B prop) (: C prop) (: D prop) (: X prop)))
; Each rule becomes an axiom: premises -> conclusion.
(= (imps $ctx $goal)
   (if (== $ctx ()) $goal
       (let* (($h (car-atom $ctx)) ($t (cdr-atom $ctx)) ($r (imps $t $goal))) (imp $h $r)){$h $t $r}))
(= (rule-statement $r) (let ($ps $c) (rule-shape $r) (imps $ps $c)))
; The kernel's rules, assumed: statements about uninterpreted propositions.
!(map-atom (axA1 axA2 ab axB bc xc pair) $r
   (let $s (rule-statement $r) (set:axiom &thy $r $s)))

(= (hyp-name 0) h0)
(= (hyp-name 1) h1)
(= (hyp-name 2) h2)
; A derivation with holes as a proof term: hole i is hypothesis i, a rule is
; its axiom applied to the proofs of its premises.
(= (to-proof $t)
   (if (is-hole $t) (let $i (index-atom $t 1) (hyp-name $i)){$i}
       (let* (($r (head-of $t)) ($ks (kids-of $t))) (apply-proofs (pf:known $r) $ks)){$r $ks}))
(= (apply-proofs $f $ks)
   (if (== $ks ()) $f
       (let* (($k (car-atom $ks)) ($pk (to-proof $k)) ($rest (cdr-atom $ks)))
         (apply-proofs (pf:imp-elim $f $pk) $rest)){$k $pk $rest}))
(= (assume-all $i $ctx $body)
   (if (== $ctx ()) (to-proof $body)
       (let* (($n (hyp-name $i)) ($t (cdr-atom $ctx)) ($j (+ $i 1)) ($inner (assume-all $j $t $body)))
         (pf:assume $n $inner)){$n $t $j $inner}))

; 8. The generalization bc(?B) is a proof of B -> C (Truth.plan_sound: the goal
;    holds whenever the obligations do).
!(let* (($l (left-plan)) ($r (right-plan)) (((plan $o $b) $ls $rs) (generalize $l $r C))
        ($s (imps $o C)) ($p (assume-all 0 $o $b)))
   (pf:theorem &thy method-slot $s $p))
; 9. Completion is application: each original's evidence, applied to it,
;    proves C (mem_completions_iff_discharge) ...
!(let* (($l (left-plan)) ($r (right-plan)) (($g $ls $rs) (generalize $l $r C))
        ($el (car-atom $ls)) ($er (car-atom $rs)) ($pl (to-proof $el)) ($pr (to-proof $er)))
   ((pf:proves &thy (pf:imp-elim (pf:known method-slot) $pl) C)
    (pf:proves &thy (pf:imp-elim (pf:known method-slot) $pr) C)))
; ... and evidence for another judgment does not (wire_wrong_evidence_rejected).
!(pf:proves &thy (pf:imp-elim (pf:known method-slot) (pf:known axA1)) C)

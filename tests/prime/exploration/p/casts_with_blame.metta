; Proof plans with casts: a cast closes only with its obligation, and a cast
; that cannot close is blamed by position.
;
; The kernel is a small proof calculus with nullary judgments; each rule is
; (rule name premises conclusion):
;   axA1, axA2 : |- A      ab : A |- B          axB : |- B
;   bc : B |- C            xc : X |- C          pair : A, A |- D
;   both : A, B |- E       loop : |- L          weak : L |- W
;   castCG : CastCG, C |- G      castAB : CastAB, A |- B     axCastAB : |- CastAB
; No rule concludes X or CastCG.  A countermodel makes X, CastCG and G false
; and every other judgment true; every rule preserves it, so X, CastCG and G
; have no derivation.
;
; A plan is (plan obligations body); a hole is an undeclared head applied to
; what it depends on, as in the round-2 hole fixtures: (? i) is obligation i.
; A cast from an established judgment phi to a target psi is a plan through a
; cast rule Cast(phi, psi), phi |- psi whose obligation is the cast itself:
;   the cast C -> G   (plan (CastCG) (castCG (? 0) (bc (ab axA1))))  goal G
;   the cast A -> B   (plan (CastAB) (castAB (? 0) axA1))            goal B
; Closing a plan searches evidence for its obligations by backward chaining
; over the kernel rules, in order, to a fixed depth; if the search fails and
; the countermodel refutes an obligation, the verdict is Refuted and names
; that obligation's position; otherwise Incomplete.  No cast is discharged
; silently: identifying the obligation CastCG with the derivable CastAB is a
; term the checker rejects.
;
; The ITP half reads a plan as a proof of the implication from its
; obligations to its goal, hole i being hypothesis i.
;
; Mirrors: bypass_closed_iff, bypass_blamed, castAB_closes,
; kernelRules_preserve_truth, no_derivation_castCG, no_derivation_G,
; no_silent_identification, Truth.completions_empty, Truth.plan_sound.
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
(rule both (A B) E)
(rule loop () L)
(rule weak (L) W)
(rule castCG (CastCG C) G)
(rule castAB (CastAB A) B)
(rule axCastAB () CastAB)

(= (rule-names) (axA1 axA2 ab axB bc xc pair both loop weak castCG castAB axCastAB))
(= (rule-shape $r)
   (let $found (collapse (match &self (rule $r $ps $c) ($ps $c)))
     (if (== $found ()) unknown-rule (car-atom $found))))

; The countermodel.
(= (holds $j) (if (== $j X) False (if (== $j CastCG) False (if (== $j G) False True))))
(= (all-hold $js) (if (== $js ()) True (let* (($h (car-atom $js)) ($t (cdr-atom $js))) (and (holds $h) (all-hold $t)))))

; ---- lists and derivations --------------------------------------------------------

(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (append $t $ys))) (cons-atom $h $r))))
(= (nth $xs $i)
   (if (== $xs ()) out-of-range
       (if (== $i 0) (car-atom $xs) (let* (($t (cdr-atom $xs)) ($j (- $i 1))) (nth $t $j)))))
(= (is-hole $t)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) False (let $h (car-atom $t) (== $h ?)))
       False))
(= (head-of $t) (if (== (get-metatype $t) Symbol) $t (car-atom $t)))
(= (kids-of $t) (if (== (get-metatype $t) Symbol) () (cdr-atom $t)))
(= (rebuild $r $kids) (if (== $kids ()) $r (cons-atom $r $kids)))
(= (conclusion $ctx $t)
   (if (is-hole $t)
       (let $i (index-atom $t 1) (nth $ctx $i))
       (let* (($r (head-of $t)) ($kids (kids-of $t)) ($shape (rule-shape $r)))
         (if (== $shape unknown-rule) ill-formed
             (let* ((($ps $c) $shape)
                    ($got (map-atom $kids $k (conclusion $ctx $k))))
               (if (== $got $ps) $c ill-formed))))))
(= (fill $t $ev)
   (if (is-hole $t)
       (let $i (index-atom $t 1) (nth $ev $i))
       (let* (($r (head-of $t)) ($kids (kids-of $t))
              ($filled (map-atom $kids $k (fill $k $ev))))
         (rebuild $r $filled))))

; ---- closing a plan ------------------------------------------------------------------

; Backward chaining over the kernel rules, in order, to depth d: the first
; derivation found, or none.
(= (derive $j $d)
   (if (== $d 0) none
       (let $rs (rule-names) (derive-with $rs $j $d))))
(= (derive-with $rs $j $d)
   (if (== $rs ()) none
       (let* (($r (car-atom $rs)) ($rest (cdr-atom $rs)) (($ps $c) (rule-shape $r)))
         (if (== $c $j)
             (let* (($d2 (- $d 1)) ($kids (derive-all $ps $d2)))
               (if (== $kids none) (derive-with $rest $j $d) (rebuild $r $kids)))
             (derive-with $rest $j $d)))))
(= (derive-all $js $d)
   (if (== $js ()) ()
       (let* (($j (car-atom $js)) ($x (derive $j $d)))
         (if (== $x none) none
             (let* (($t (cdr-atom $js)) ($xs (derive-all $t $d)))
               (if (== $xs none) none (cons-atom $x $xs)))))))
; The first obligation the countermodel refutes, with its position.
(= (refuted-obligation $ctx $i)
   (if (== $ctx ()) none
       (let* (($j (car-atom $ctx)) ($t (cdr-atom $ctx)) ($k (+ $i 1)))
         (if (holds $j) (refuted-obligation $t $k) (blame $i $j)))))
(= (close-plan (plan $ctx $body) $depth)
   (let $ev (derive-all $ctx $depth)
     (if (== $ev none)
         (let $r (refuted-obligation $ctx 0)
           (if (== $r none) (Incomplete $depth) (Refuted $r)))
         (let $d (fill $body $ev) (Established $d)))))

(= (bypass-plan) (plan (CastCG) (castCG (? 0) (bc (ab axA1)))))
(= (cast-ab-plan) (plan (CastAB) (castAB (? 0) axA1)))

; 1. Every rule preserves the countermodel (kernelRules_preserve_truth), so
;    the refutations below are sound.
!(let $rs (rule-names)
   (foldl-atom $rs True $ok $r (let ($ps $c) (rule-shape $r) (and $ok (if (all-hold $ps) (holds $c) True)))))
; 2. The cast C -> G is a plan for G whose one obligation is the cast.
!(let (plan $ctx $body) (bypass-plan) ($ctx (conclusion $ctx $body)))
; 3. It never closes: its obligation is refuted, and the refutation names
;    position 0 (bypass_blamed, no_derivation_castCG).
!(let $p (bypass-plan) (close-plan $p 4))
; 4. The cast A -> B closes with its obligation (castAB_closes), and the
;    completion is a derivation of B.
!(let $p (cast-ab-plan) (close-plan $p 4))
!(conclusion () (castAB axCastAB axA1))
; 5. A cast plan closes exactly when its cast obligation does
;    (bypass_closed_iff): for each cast, (plan closes, obligation closes).
(= (closes $v) (let $s (car-atom $v) (== $s Established)))
!(let* (($p1 (bypass-plan)) ($p2 (cast-ab-plan))
        ($v1 (close-plan $p1 4)) ($v2 (close-plan $p2 4))
        ($o1 (close-plan (plan (CastCG) (? 0)) 4)) ($o2 (close-plan (plan (CastAB) (? 0)) 4)))
   (((closes $v1) (closes $o1)) ((closes $v2) (closes $o2))))
; 6. No silent identification: CastAB and C are derivable and G is not, so
;    evidence for CastAB is not evidence for CastCG; the term that uses it
;    that way is rejected (no_silent_identification).
!((holds CastAB) (holds C) (holds G))
!(conclusion () (castCG axCastAB (bc (ab axA1))))

; ---- the ITP half: a plan is a proof of an implication ----------------------------------

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: A prop) (: B prop) (: C prop) (: CastAB prop) (: CastCG prop) (: G prop)))
(= (imps $ctx $goal)
   (if (== $ctx ()) $goal
       (let* (($h (car-atom $ctx)) ($t (cdr-atom $ctx)) ($r (imps $t $goal))) (imp $h $r))))
(= (rule-statement $r) (let ($ps $c) (rule-shape $r) (imps $ps $c)))
; The kernel's rules, assumed: statements about uninterpreted propositions.
!(map-atom (axA1 ab bc castCG castAB axCastAB) $r
   (let $s (rule-statement $r) (set:axiom &thy $r $s)))
(= (hyp-name 0) h0)
(= (hyp-name 1) h1)
(= (to-proof $t)
   (if (is-hole $t) (let $i (index-atom $t 1) (hyp-name $i))
       (let* (($r (head-of $t)) ($ks (kids-of $t))) (apply-proofs (pf:known $r) $ks))))
(= (apply-proofs $f $ks)
   (if (== $ks ()) $f
       (let* (($k (car-atom $ks)) ($pk (to-proof $k)) ($rest (cdr-atom $ks)))
         (apply-proofs (pf:imp-elim $f $pk) $rest))))
(= (assume-all $i $ctx $body)
   (if (== $ctx ()) (to-proof $body)
       (let* (($n (hyp-name $i)) ($t (cdr-atom $ctx)) ($j (+ $i 1)) ($inner (assume-all $j $t $body)))
         (pf:assume $n $inner))))

; 7. The cast C -> G is a proof of CastCG -> G: the target holds whenever the
;    cast does (Truth.plan_sound).
!(let* (((plan $ctx $body) (bypass-plan)) ($s (imps $ctx G)) ($p (assume-all 0 $ctx $body)))
   (pf:theorem &thy bypass $s $p))
; 8. The cast A -> B, applied to its closed obligation, proves B
;    (castAB_closes).
!(let $p (to-proof (castAB axCastAB axA1)) (pf:theorem &thy b-by-cast B $p))
; 9. Discharging the cast C -> G with the evidence for CastAB is refused
;    (no_silent_identification).
!(let $p (to-proof (castCG axCastAB (bc (ab axA1)))) (pf:theorem &thy g-silently G $p))

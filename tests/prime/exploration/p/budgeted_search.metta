(set:profile hol)

; Budgeted plan search: exhaustion is Incomplete, never Refuted.
;
; The kernel is a small proof calculus with nullary judgments; each rule is
; (rule name premises conclusion):
;   axA1, axA2 : |- A      ab : A |- B      axB : |- B
;   bc : B |- C            xc : X |- C      castCG : CastCG, C |- G
; No rule concludes X or CastCG.  A countermodel makes X, CastCG and G false
; and every other judgment true; every rule preserves it.
;
; Search is backward chaining with a depth budget over a library of candidate
; rules, tried in library order: at budget 0 nothing is found; at budget n + 1
; a goal is found through the first candidate concluding it whose premises
; are all found at budget n.  The kernel library is axA1, axA2, ab, axB, bc,
; xc, in that order; the reversed library is the same rules backwards.
;
; A plan is (plan obligations body); a hole is an undeclared head applied to
; what it depends on, as in the round-2 hole fixtures: (? i) is obligation i.
; The verdict of a plan at a budget is an authority outcome:
;   (Established d)  the search found evidence for every obligation, and d is
;                    the plan completed with it;
;   (Refuted o)      the search failed and a refuter certified that
;                    obligation o has no derivation;
;   (Incomplete n)   the search failed at budget n and nothing was refuted.
; Refuters: the countermodel (a judgment it makes false has no derivation),
; a refuter that knows only X, and the silent refuter.  The naive status
; reports exhaustion as a refutation.
;   assume-c   (plan (C) (? 0))                        the plan that assumes C
;   plan-xc    (plan (X) (xc (? 0)))
;   bypass     (plan (CastCG) (castCG (? 0) (bc (ab axA1))))
;
; Mirrors: search_complete, exhausted_no_shallow_evidence,
; search_isSome_perm, verdict_not_refuted, verdict_refuted_sound,
; verdict_budgetRefines, assumeC_incomplete_one, assumeC_established_two,
; naive_refutation_unsound, planXC_refuted, bypass_incomplete,
; ranking_changes_provenance, mem_completions_assume,
; verdict_established_sound.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- the kernel ------------------------------------------------------------------

(rule axA1 () A)
(rule axA2 () A)
(rule ab (A) B)
(rule axB () B)
(rule bc (B) C)
(rule xc (X) C)
(rule castCG (CastCG C) G)

(= (rule-shape $r)
   (let $found (collapse (match &self (rule $r $ps $c) ($ps $c)))
     (if (== $found ()) unknown-rule (car-atom $found))))
(= (holds $j) (if (== $j X) False (if (== $j CastCG) False (if (== $j G) False True))))

; ---- derivations with holes ----------------------------------------------------------

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

; ---- backward chaining with a budget ---------------------------------------------------

(= (kernel-library) (axA1 axA2 ab axB bc xc))
(= (reversed-library) (xc bc axB ab axA2 axA1))
(= (search $lib $fuel $goal)
   (if (== $fuel 0) none
       (let $f (- $fuel 1) (first-found $lib $lib $f $goal))))
(= (first-found $cands $lib $f $goal)
   (if (== $cands ()) none
       (let* (($r (car-atom $cands)) ($rest (cdr-atom $cands)) ($x (try-candidate $lib $f $goal $r)))
         (if (== $x none) (first-found $rest $lib $f $goal) $x))))
(= (try-candidate $lib $f $goal $r)
   (let ($ps $c) (rule-shape $r)
     (if (== $c $goal)
         (let $kids (search-premises $lib $f $ps)
           (if (== $kids none) none (rebuild $r $kids)))
         none)))
(= (search-premises $lib $f $ps)
   (if (== $ps ()) ()
       (let* (($p (car-atom $ps)) ($x (search $lib $f $p)))
         (if (== $x none) none
             (let* (($t (cdr-atom $ps)) ($xs (search-premises $lib $f $t)))
               (if (== $xs none) none (cons-atom $x $xs)))))))
(= (found $lib $fuel $goal) (let $x (search $lib $fuel $goal) (not (== $x none))))

; ---- verdicts --------------------------------------------------------------------------

(= (refutes countermodel $j) (not (holds $j)))
(= (refutes x-only $j) (== $j X))
(= (refutes silent $j) False)
(= (refuted-obligation $ref $ctx $i)
   (if (== $ctx ()) none
       (let* (($j (car-atom $ctx)) ($t (cdr-atom $ctx)) ($k (+ $i 1)))
         (if (refutes $ref $j) (obligation $i $j) (refuted-obligation $ref $t $k)))))
(= (plan-verdict $lib $ref (plan $ctx $body) $fuel)
   (let $ev (search-premises $lib $fuel $ctx)
     (if (== $ev none)
         (let $r (refuted-obligation $ref $ctx 0)
           (if (== $r none) (Incomplete $fuel) (Refuted $r)))
         (let $d (fill $body $ev) (Established $d)))))
(= (naive-status $lib (plan $ctx $body) $fuel)
   (let $ev (search-premises $lib $fuel $ctx) (if (== $ev none) Refuted Established)))
(= (status $v) (car-atom $v))

(= (assume-c) (plan (C) (? 0)))
(= (plan-xc) (plan (X) (xc (? 0))))
(= (bypass) (plan (CastCG) (castCG (? 0) (bc (ab axA1)))))

; 1. The plan that assumes C is Incomplete at budget 1: no library derivation
;    of C has depth 1 (assumeC_incomplete_one, exhausted_no_shallow_evidence).
!(let* (($lib (kernel-library)) ($p (assume-c))) (plan-verdict $lib countermodel $p 1))
; 2. At budget 2 it is Established, by bc(axB) (assumeC_established_two,
;    search_complete).
!(let* (($lib (kernel-library)) ($p (assume-c))) (plan-verdict $lib countermodel $p 2))
; 3. Raising the budget refines the verdict: Incomplete may become
;    Established, and Established stays (verdict_budgetRefines).
!(let* (($lib (kernel-library)) ($p (assume-c)))
   (map-atom (0 1 2 3 4) $n (let $v (plan-verdict $lib countermodel $p $n) (status $v))))
; 4. Reporting exhaustion as refutation is unsound: the naive status refutes
;    the plan that assumes C at budget 1, and bc(ab(axA1)) is a derivation of
;    C, so a completion of that plan (naive_refutation_unsound,
;    mem_completions_assume).
!(let* (($lib (kernel-library)) ($p (assume-c)))
   ((naive-status $lib $p 1) (conclusion () (bc (ab axA1)))))
; 5. A refuter holding a countermodel refutes xc(?X) at every budget, naming
;    the refuted obligation (planXC_refuted, verdict_refuted_sound).
!(let* (($lib (kernel-library)) ($p (plan-xc))) (map-atom (0 1 5) $n (plan-verdict $lib countermodel $p $n)))
; 6. A refuter that knows only X leaves the bypass Incomplete at every budget,
;    although its obligation CastCG is false (bypass_incomplete).
!(let* (($lib (kernel-library)) ($p (bypass))) (map-atom (0 1 5) $n (plan-verdict $lib x-only $p $n)))
; 7. Without a refutation the verdict is never Refuted, at any budget
;    (verdict_not_refuted): with the silent refuter, three plans at budgets 0
;    to 4.
!(let* (($lib (kernel-library)) ($ps ((assume-c) (plan-xc) (bypass))))
   (let $vs (collapse (let* (($p (superpose $ps)) ($n (superpose (0 1 2 3 4))))
                        (let $v (plan-verdict $lib silent $p $n) (status $v))))
     (foldl-atom $vs 0 $k $s (if (== $s Refuted) (+ $k 1) $k))))

; ---- ranking --------------------------------------------------------------------------

; 8. Reordering the candidates changes which derivation is found: B at budget
;    2 is ab(axA1) in the kernel order and axB in the reversed order
;    (ranking_changes_provenance) ...
!(let* (($k (kernel-library)) ($r (reversed-library))) ((search $k 2 B) (search $r 2 B)))
; ... and never whether one is found: every goal, every budget from 0 to 4
;    (search_isSome_perm).
!(let* (($k (kernel-library)) ($r (reversed-library))
        ($bad (collapse (let* (($g (superpose (A B C X G CastCG))) ($n (superpose (0 1 2 3 4))))
                          (if (== (found $k $n $g) (found $r $n $g)) (empty) ($g $n))))))
   $bad)

; ---- the ITP half: an established verdict carries a checked proof -------------------------

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: A prop) (: B prop) (: C prop) (: X prop)))
(= (imps $ctx $goal)
   (if (== $ctx ()) $goal
       (let* (($h (car-atom $ctx)) ($t (cdr-atom $ctx)) ($r (imps $t $goal))) (imp $h $r))))
(= (rule-statement $r) (let ($ps $c) (rule-shape $r) (imps $ps $c)))
; The kernel's rules, assumed: statements about the uninterpreted A, B, C, X.
!(let $lib (kernel-library) (map-atom $lib $r (let $s (rule-statement $r) (set:axiom &thy $r $s))))
(= (to-proof $t)
   (let* (($r (head-of $t)) ($ks (kids-of $t))) (apply-proofs (pf:known $r) $ks)))
(= (apply-proofs $f $ks)
   (if (== $ks ()) $f
       (let* (($k (car-atom $ks)) ($pk (to-proof $k)) ($rest (cdr-atom $ks)))
         (apply-proofs (pf:imp-elim $f $pk) $rest))))

; 9. The completion carried by the established verdict at budget 2 is a proof
;    of C that the kernel checks (verdict_established_sound) ...
!(let* (($lib (kernel-library)) ($p (assume-c)) ((Established $d) (plan-verdict $lib countermodel $p 2))
        ($pf (to-proof $d)))
   (pf:proves &thy $pf C))
; ... and so is bc(ab(axA1)), the completion of the plan the naive status
;     refuted at budget 1 (naive_refutation_unsound).
!(let $pf (to-proof (bc (ab axA1))) (pf:proves &thy $pf C))

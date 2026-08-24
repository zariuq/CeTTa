; ============================================================================
; PLN-graded backward chaining, DEFINED IN-LANGUAGE (runs today)
; ============================================================================
;
; Nil-style chaining with the graded-where semantics from file 03, built as
; a PoC for native PLN-guided inference.  The knowledge base is DATA:
;
;   (fact $conclusion $weight)            evidence, weighted (s·c powers)
;   (rule $name ($premises) $concl $w)    rules, weighted (rule trust)
;
; Derivation weights are ordinary weight-semiring products: the path weight
; of a proof is the ⊗-product of the rule trusts and fact weights along it
; (conjunction).  Competing derivations of the same goal form a V-weighted
; bag of proofs; the discipline (argmax here) selects the best proof —
; which is exactly what "PLN guides the search" means, expressed in the
; where-clause idiom: grade at the source, discipline named and swappable.
;
; Mechanism worth noting: one match call does double duty —
;   (match <goal-space> <rule-conclusion> <rule-premises>)
; filters rules to those whose conclusion unifies with the goal AND
; instantiates their premises with the goal's bindings.
;
; Positive and negative examples both run at the end.
;
; ---------------------------------------------------------------------------
; 0. Machinery (CP2 idiom: let-bind before grounded utils)
; ---------------------------------------------------------------------------

(= (force-value $v) $v)

(= (better $x $y)
   (if (> (car-atom $x) (car-atom $y)) $x $y))

(= (argmax $xs)
   (foldl-atom $xs (car-atom $xs) $acc $x (better $acc $x)))

(= (append2 $xs $ys)
   (if (== $xs ()) $ys
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $r (append2 $t $ys)
             (cons-atom $h $r))))))

; ---------------------------------------------------------------------------
; 1. Derivation, with weights (the graded where-clause per inference step)
; ---------------------------------------------------------------------------

; Fact candidates: (weight . proof) rows directly from the knowledge base.
(= (fact-candidates $kb $goal)
   (match $kb (fact $goal $w)
     ($w (fact-of $goal))))

; Weighted premises: best proof per premise; total weight = product.
; Returns ((subproofs...) weight); weight 0 signals an unprovable premise.
(= (derive-all $kb $prems $depth)
   (if (== $prems ())
       (() 1)
       (let $h (car-atom $prems)
         (let $t (cdr-atom $prems)
           (let ($hp $hw) (best-proof $kb $h $depth)
             (if (== $hw ())
                 (() 0)
                 (let ($tp $tw) (derive-all $kb $t $depth)
                   (let $r (cons-atom ($hw $hp) $tp)
                     ($r (* $hw $tw))))))))))

; Rule candidates: match rule triples; use the rule's own conclusion to
; match the goal, and its premises as the template -> instantiated premises
; in one step.  Weight = rule trust × product of premise weights.
(= (rule-candidates $kb $goal $depth)
   (if (> $depth 0)
       (let $gs (new-space)
         (let $gi (add-atom $gs $goal)
           (match $kb (rule $r $prems $concl $w)
             (let $iprems (match $gs $concl $prems)
               (let $d2 (- $depth 1)
                 (let ($subs $sw) (derive-all $kb $iprems $d2)
                   (if (> $sw 0)
                       ((* $w $sw) (by $r $subs))
                       ())))))))
       (())))

(= (drop-empties $xs)
   (if (== $xs ()) ()
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $r (drop-empties $t)
             (if (== $h ()) $r (cons-atom $h $r)))))))

(= (all-candidates $kb $goal $depth)
   (drop-empties
     (append2 (collapse (fact-candidates $kb $goal))
              (collapse (rule-candidates $kb $goal $depth)))))

; The discipline: argmax over the weighted proof bag.
; Returns (proof weight); (() ()) when nothing is provable — abstention,
; not error (the graded where's zero).
(= (best-proof $kb $goal $depth)
   (let $cands (all-candidates $kb $goal $depth)
     (if (== $cands ())
         (() ())
         (best-pair $cands))))

(= (best-pair $xs)
   (let ($w $p) (argmax $xs)
     ($p $w)))

; ---------------------------------------------------------------------------
; 2. A small PLN-flavored knowledge base
; ---------------------------------------------------------------------------

(= (seed-kb $kb)
   (let $a (add-atom $kb (fact (man socrates) 0.9))
     (let $b (add-atom $kb (fact (man plato) 0.8))
       (let $c (add-atom $kb (fact (greek socrates) 0.95))
         (let $d (add-atom $kb (rule mortal-1 ((man $x)) (mortal-x $x) 0.85))
           (let $e (add-atom $kb (rule human-1 ((greek $x)) (human-x $x) 0.9))
             (add-atom $kb (rule mortal-2 ((human-x $x) (man $x)) (mortal-x $x) 0.7))))))))

(= (prove $goal)
   (let $kb (new-space)
     (let $s (seed-kb $kb)
       (best-proof $kb $goal 4))))

(= (show-bag $goal)
   (let $kb (new-space)
     (let $s (seed-kb $kb)
       (all-candidates $kb $goal 4))))

; ---------------------------------------------------------------------------
; 3. Positive and negative examples
; ---------------------------------------------------------------------------

; (mortal-x socrates) has TWO derivations:
;   via mortal-1:  0.85 × 0.9                              = 0.765
;   via mortal-2:  0.7 × (0.855 × 0.9) where human-x = 0.9 × 0.95 → 0.5387
; argmax must pick mortal-1's proof — and show its trust.
!(prove (mortal-x socrates))

; The human route on its own goal (two rules deep? no: one rule + fact):
; expected weight = 0.9 × 0.95 = 0.855.
!(prove (human-x socrates))

; NEGATIVE: plato has no greek fact — unprovable, abstention (() ()),
; not an error.
!(prove (human-x plato))

; NEGATIVE control: a totally unknown head — likewise abstains.
!(prove (martian-x socrates))

; THE BAG, unselected: everything the where-clause hands the discipline —
; the weighted candidate set itself, with the numbers visible.
!(show-bag (mortal-x socrates))

; [WISHLIST] native form — one declaration instead of seed-kb, discipline
; folded into the space profile:
;
;   (space &kb (weight-semiring propensity) (discipline (argmax-by grade)))
;   (= (mortal-x $x) (use (man $x)) (grade (rule-trust mortal-1)))
;
; Performance note (PoC, not native): all-candidates materializes the whole
; bag per step — fine at this size; native resolves per-candidate at the
; seam.  Constant-factor difference, identical semantics.

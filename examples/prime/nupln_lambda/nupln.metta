;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; nuPLN with lambda predicates                                              ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Nil Geisweiller's experimental/nupln/nupln.metta, with one change: a
;; predicate is an ordinary function instead of a combinator term.
;;
;;   combinators:   (∧ F G)
;;   here:          (lam $x (∧ (F $x) (G $x)))
;;
;; Everything else is Nil's: the knowledge base, the evidence, the statement
;; form (→ (⊙ x) P) for "P holds of x", the proof constructors cet, cefl, cefr
;; and ",", the backward chainer nupln_bc with its derivation sizes, and the
;; eleven tests with his budgets.  Learning is his last test unchanged: the
;; same chainer is asked the same question with the predicate left unknown.
;;
;; Two rules replace the combinator rules, and no substitution is needed:
;;
;;   * a conjunction under a binder splits into two functions under the same
;;     binder: (lam x (∧ a b)) holds of c when (lam x a) and (lam x b) do;
;;   * a predicate applied to the bound variable is that predicate (η):
;;     (lam x (P x)) holds of c when P does, which is Nil's evidence.
;;
;; The η step costs nothing, since (lam x (P x)) and P are the same function,
;; so every derivation has the size Nil's combinator version gives it.  The
;; η step consults the knowledge base directly, so an unknown predicate cannot
;; unfold into nested lambdas without bound.
;;
;; After Nil's eleven tests the file takes the step his file names as NEXT:
;; normal forms by extra premises, so that learning finds one hypothesis
;; instead of mirror images, and finds it faster.
;;
;; What is better: the learned hypothesis is a function.  After learning, the
;; file gives F and G executable definitions and calls the learned lambda on
;; values it never saw evidence about.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; nuPLN Backward Chainer (Nil's)     ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Base case: a fact of the knowledge base.
(= (nupln_bc $kb $budget (: $x $a))
   (if (<= 1 $budget)
       (match $kb (: $x $a) (MkSized 1 (: $x $a)))
       (empty)))

;; Type check a conjunction under a binder.  Both conjuncts, under the same
;; binder, must be predicates of the same domain.
(= (nupln_bc $kb $budget (: (lam $v (∧ $f $g)) (-> $a Bool)))
   (if (<= 3 $budget)
       (let* (((MkSized $fs (: (lam $v $f) (-> $a Bool)))
               (nupln_bc $kb (- $budget 2) (: (lam $v $f) (-> $a Bool))))
              ((MkSized $gs (: (lam $v $g) (-> $a Bool)))
               (nupln_bc $kb (- $budget (+ $fs 1)) (: (lam $v $g) (-> $a Bool)))))
         (MkSized (+ 1 (+ $fs $gs)) (: (lam $v (∧ $f $g)) (-> $a Bool))))
       (empty)))

;; Type check a predicate applied to the bound variable (η): it has the type
;; of the predicate, found in the knowledge base.
(= (nupln_bc $kb $budget (: (lam $v ($p $v)) (-> $a Bool)))
   (if (<= 1 $budget)
       (match $kb (: $p (-> $a Bool))
         (MkSized 1 (: (lam $v ($p $v)) (-> $a Bool))))
       (empty)))

;; Type check singleton construction, ⊙.  If $x is of type $a, then
;; (⊙ $x) is of type $a -> Bool.
(= (nupln_bc $kb $budget (: (⊙ $x) (-> $a Bool)))
   (if (<= 2 $budget)
       (let (MkSized $xs (: $x $a)) (nupln_bc $kb (- $budget 1) (: $x $a))
            (MkSized (+ 1 $xs) (: (⊙ $x) (-> $a Bool))))
       (empty)))

;; Type check implication, →: a statement relating two predicates of the
;; same type.
(= (nupln_bc $kb $budget (: (→ $f $g) Statement))
   (if (<= 3 $budget)
       (let* (((MkSized $fs (: $f $a))
               (nupln_bc $kb (- $budget 2) (: $f $a)))
              ((MkSized $gs (: $g $a))
               (nupln_bc $kb (- $budget (+ $fs 1)) (: $g $a))))
         (MkSized (+ 1 (+ $fs $gs)) (: (→ $f $g) Statement)))
       (empty)))

;; Conjunction evaluation to ⊤.  The conjunction holds of $x when both
;; conjuncts, under the same binder, hold of $x.  `cet` stands for
;; conjunction evaluates to true.
;;
;; The three evaluation rules prove their premises in a different order from
;; Nil's: the evidence first, the typing of the statement last.  Derivations
;; and sizes are the same.  When the predicate is unknown, the evidence then
;; builds it, instead of every well-typed predicate being enumerated first.
(= (nupln_bc $kb $budget (: (cet $fp $gp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊤)))
   (if (<= 4 $budget)
       (let* (((MkSized $fs (: $fp (≞ (→ (⊙ $x) (lam $v $f)) ⊤)))
               (nupln_bc $kb (- $budget 3) (: $fp (≞ (→ (⊙ $x) (lam $v $f)) ⊤))))
              ((MkSized $gs (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊤)))
               (nupln_bc $kb (- $budget (+ $fs 2)) (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊤))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))
               (nupln_bc $kb (- $budget (+ (+ $fs $gs) 1)) (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))))
         (MkSized (+ 1 (+ $as (+ $fs $gs)))
                  (: (cet $fp $gp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊤))))
       (empty)))

;; Conjunction evaluation to ⊥, left: the left conjunct is false of $x.
;; `cefl` stands for conjunction evaluates to false from left.
(= (nupln_bc $kb $budget (: (cefl $fp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊥)))
   (if (<= 3 $budget)
       (let* (((MkSized $ps (: $fp (≞ (→ (⊙ $x) (lam $v $f)) ⊥)))
               (nupln_bc $kb (- $budget 2) (: $fp (≞ (→ (⊙ $x) (lam $v $f)) ⊥))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))
               (nupln_bc $kb (- $budget (+ $ps 1)) (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))))
         (MkSized (+ 1 (+ $as $ps)) (: (cefl $fp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊥))))
       (empty)))

;; Conjunction evaluation to ⊥, right: the right conjunct is false of $x.
;; `cefr` stands for conjunction evaluates to false from right.
(= (nupln_bc $kb $budget (: (cefr $gp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊥)))
   (if (<= 3 $budget)
       (let* (((MkSized $ps (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊥)))
               (nupln_bc $kb (- $budget 2) (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊥))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))
               (nupln_bc $kb (- $budget (+ $ps 1)) (: (→ (⊙ $x) (lam $v (∧ $f $g))) $a))))
         (MkSized (+ 1 (+ $as $ps)) (: (cefr $gp) (≞ (→ (⊙ $x) (lam $v (∧ $f $g))) ⊥))))
       (empty)))

;; Evaluation of a predicate applied to the bound variable (η): the evidence
;; for the predicate itself at $x.
(= (nupln_bc $kb $budget (: $pp (≞ (→ (⊙ $x) (lam $v ($p $v))) $tv)))
   (if (<= 1 $budget)
       (match $kb (: $pp (≞ (→ (⊙ $x) $p) $tv))
         (MkSized 1 (: $pp (≞ (→ (⊙ $x) (lam $v ($p $v))) $tv))))
       (empty)))

;; Product.  Both $a and $b must be proven for (, $a $b) to be proven.
(= (nupln_bc $kb $budget (: (, $x $y) (, $a $b)))
   (if (<= 3 $budget)
       (let* (((MkSized $xs (: $x $a))
               (nupln_bc $kb (- $budget 2) (: $x $a)))
              ((MkSized $ys (: $y $b))
               (nupln_bc $kb (- $budget (+ $xs 1)) (: $y $b))))
         (MkSized (+ 1 (+ $xs $ys)) (: (, $x $y) (, $a $b))))
       (empty)))

;; A test passes when the answer equals the expected one up to the names of
;; its variables.
(= (test $answer $expected)
   (unify $answer $expected ✅ (❌ $answer)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Knowledge base (Nil's, unchanged)      ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

!(bind! &kb (new-space))
!(add-atom &kb (: 0 UInt))
!(add-atom &kb (: 1 UInt))
!(add-atom &kb (: 2 UInt))
!(add-atom &kb (: F (-> UInt Bool)))
!(add-atom &kb (: G (-> UInt Bool)))
!(add-atom &kb (: f0t (≞ (→ (⊙ 0) F) ⊤)))   ; F holds of 0
!(add-atom &kb (: f1f (≞ (→ (⊙ 1) F) ⊥)))   ; F is false of 1
!(add-atom &kb (: f2t (≞ (→ (⊙ 2) F) ⊤)))   ; F holds of 2
!(add-atom &kb (: g0t (≞ (→ (⊙ 0) G) ⊤)))   ; G holds of 0
!(add-atom &kb (: g1t (≞ (→ (⊙ 1) G) ⊤)))   ; G holds of 1
!(add-atom &kb (: g2f (≞ (→ (⊙ 2) G) ⊥)))   ; G is false of 2

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Nil's eleven tests, the conjunction written as a lambda      ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; 1. Type check F
!(test
  (nupln_bc &kb 1 (: F $a))
  (MkSized 1 (: F (-> UInt Bool))))

;; 2. Type check G
!(test
  (nupln_bc &kb 1 (: G $a))
  (MkSized 1 (: G (-> UInt Bool))))

;; 3. Type check the conjunction of F and G
!(test
  (nupln_bc &kb 3 (: (lam $x (∧ (F $x) (G $x))) $a))
  (MkSized 3 (: (lam $x (∧ (F $x) (G $x))) (-> UInt Bool))))

;; 4. Type check (⊙ 0)
!(test
  (nupln_bc &kb 2 (: (⊙ 0) $a))
  (MkSized 2 (: (⊙ 0) (-> UInt Bool))))

;; 5. Type check the statement that the conjunction holds of 0
!(test
  (nupln_bc &kb 6 (: (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) $a))
  (MkSized 6 (: (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) Statement)))

;; 6. Evaluate the conjunction over 0
!(test
  (nupln_bc &kb 9 (: $prf (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) $tv)))
  (MkSized 9 (: (cet f0t g0t) (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤))))

;; 7. Evaluate the conjunction over 1
!(test
  (nupln_bc &kb 8 (: $prf (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) $tv)))
  (MkSized 8 (: (cefl f1f) (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥))))

;; 8. Find proof of the evaluation of F over 3 values
!(test
  (nupln_bc &kb 5 (: $prf (, (≞ (→ (⊙ 0) F) ⊤)
                          (, (≞ (→ (⊙ 1) F) ⊥)
                             (≞ (→ (⊙ 2) F) ⊤)))))
  (MkSized 5 (: (, f0t (, f1f f2t))
                (, (≞ (→ (⊙ 0) F) ⊤)
                (, (≞ (→ (⊙ 1) F) ⊥)
                   (≞ (→ (⊙ 2) F) ⊤))))))

;; 9. Find proof of the conjunction's evaluation over 2 values
!(test
  (nupln_bc &kb 18 (: $prf (, (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤)
                              (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥))))
  (MkSized 18 (: (, (cet f0t g0t) (cefl f1f))
                 (, (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤)
                    (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥)))))

;; 10. Find proof of the conjunction's evaluation over 3 values
!(test
  (nupln_bc &kb 27 (: $prf (, (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤)
                           (, (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥)
                              (≞ (→ (⊙ 2) (lam $x (∧ (F $x) (G $x)))) ⊥)))))
  (MkSized 27 (: (, (cet f0t g0t) (, (cefl f1f) (cefr g2f)))
                 (, (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤)
                 (, (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥)
                    (≞ (→ (⊙ 2) (lam $x (∧ (F $x) (G $x)))) ⊥))))))

;; 11. Learning.  Infer what predicate could evaluate to the truth table
;; ⊤ ⊥ ⊥ over 0, 1, 2.  The predicate is left unknown; the chainer builds it.
!(test
  (once (nupln_bc &kb 27 (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤)
                                 (, (≞ (→ (⊙ 1) $pred) ⊥)
                                    (≞ (→ (⊙ 2) $pred) ⊥))))))
  (MkSized 27 (: (, (cet f0t g0t) (, (cefl f1f) (cefr g2f)))
                 (, (≞ (→ (⊙ 0) (lam $x (∧ (F $x) (G $x)))) ⊤)
                 (, (≞ (→ (⊙ 1) (lam $x (∧ (F $x) (G $x)))) ⊥)
                    (≞ (→ (⊙ 2) (lam $x (∧ (F $x) (G $x)))) ⊥))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Nil's NEXT: normal forms                                       ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Nil's file ends with:
;;
;;   NEXT: introduce reduction rules to avoid (∧ F F) and the like.
;;   Hint: instead of writing reduction rules, wire into the inference
;;   rules obligations of building components in normal form, via extra
;;   premises.  This should have the effect of pruning upfront anything
;;   that is not in normal form.
;;
;; Here is that step.  The chainer nupln_bc_nf has the rules above, with the
;; conjunction rules restricted to normal form: the left conjunct is a
;; predicate applied to the bound variable, and it comes strictly before the
;; right conjunct's first predicate in an order the knowledge base declares.
;; So (∧ F G) is built, while (∧ G F) and (∧ F F) are not.  The order is a
;; side condition of the rule, so derivation sizes do not change.

!(add-atom &kb (≺ F G))

;; The first predicate of a conjunct, and the order side condition.
(= (nf:first ($q $v)) $q)
(= (nf:first (∧ ($q $v) $r)) $q)
(= (nf:ordered $kb $p $g)
   (let $q (nf:first $g)
     (if (== (collapse (match $kb (≺ $p $q) True)) ()) False True)))

;; Base case: a fact of the knowledge base.
(= (nupln_bc_nf $kb $budget (: $x $a))
   (if (<= 1 $budget)
       (match $kb (: $x $a) (MkSized 1 (: $x $a)))
       (empty)))

;; Type check a conjunction under a binder, in normal form: the left conjunct
;; is a predicate applied to the bound variable, and it comes before the right
;; conjunct's first predicate.
(= (nupln_bc_nf $kb $budget (: (lam $v (∧ ($p $v) $g)) (-> $a Bool)))
   (if (<= 3 $budget)
       (let* (((MkSized $fs (: (lam $v ($p $v)) (-> $a Bool)))
               (nupln_bc_nf $kb (- $budget 2) (: (lam $v ($p $v)) (-> $a Bool))))
              ((MkSized $gs (: (lam $v $g) (-> $a Bool)))
               (nupln_bc_nf $kb (- $budget (+ $fs 1)) (: (lam $v $g) (-> $a Bool)))))
         (if (nf:ordered $kb $p $g)
             (MkSized (+ 1 (+ $fs $gs)) (: (lam $v (∧ ($p $v) $g)) (-> $a Bool)))
             (empty)))
       (empty)))

;; Type check a predicate applied to the bound variable (η): it has the type
;; of the predicate, found in the knowledge base.
(= (nupln_bc_nf $kb $budget (: (lam $v ($p $v)) (-> $a Bool)))
   (if (<= 1 $budget)
       (match $kb (: $p (-> $a Bool))
         (MkSized 1 (: (lam $v ($p $v)) (-> $a Bool))))
       (empty)))

;; Type check singleton construction, ⊙.  If $x is of type $a, then
;; (⊙ $x) is of type $a -> Bool.
(= (nupln_bc_nf $kb $budget (: (⊙ $x) (-> $a Bool)))
   (if (<= 2 $budget)
       (let (MkSized $xs (: $x $a)) (nupln_bc_nf $kb (- $budget 1) (: $x $a))
            (MkSized (+ 1 $xs) (: (⊙ $x) (-> $a Bool))))
       (empty)))

;; Type check implication, →: a statement relating two predicates of the
;; same type.
(= (nupln_bc_nf $kb $budget (: (→ $f $g) Statement))
   (if (<= 3 $budget)
       (let* (((MkSized $fs (: $f $a))
               (nupln_bc_nf $kb (- $budget 2) (: $f $a)))
              ((MkSized $gs (: $g $a))
               (nupln_bc_nf $kb (- $budget (+ $fs 1)) (: $g $a))))
         (MkSized (+ 1 (+ $fs $gs)) (: (→ $f $g) Statement)))
       (empty)))

;; Conjunction evaluation to ⊤.  The conjunction holds of $x when both
;; conjuncts, under the same binder, hold of $x.  `cet` stands for
;; conjunction evaluates to true.
(= (nupln_bc_nf $kb $budget (: (cet $fp $gp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊤)))
   (if (<= 4 $budget)
       (let* (((MkSized $fs (: $fp (≞ (→ (⊙ $x) (lam $v ($p $v))) ⊤)))
               (nupln_bc_nf $kb (- $budget 3) (: $fp (≞ (→ (⊙ $x) (lam $v ($p $v))) ⊤))))
              ((MkSized $gs (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊤)))
               (nupln_bc_nf $kb (- $budget (+ $fs 2)) (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊤))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))
               (nupln_bc_nf $kb (- $budget (+ (+ $fs $gs) 1)) (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))))
         (if (nf:ordered $kb $p $g)
             (MkSized (+ 1 (+ $as (+ $fs $gs)))
                  (: (cet $fp $gp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊤)))
             (empty)))
       (empty)))

;; Conjunction evaluation to ⊥, left: the left conjunct is false of $x.
;; `cefl` stands for conjunction evaluates to false from left.
(= (nupln_bc_nf $kb $budget (: (cefl $fp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊥)))
   (if (<= 3 $budget)
       (let* (((MkSized $ps (: $fp (≞ (→ (⊙ $x) (lam $v ($p $v))) ⊥)))
               (nupln_bc_nf $kb (- $budget 2) (: $fp (≞ (→ (⊙ $x) (lam $v ($p $v))) ⊥))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))
               (nupln_bc_nf $kb (- $budget (+ $ps 1)) (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))))
         (if (nf:ordered $kb $p $g)
             (MkSized (+ 1 (+ $as $ps)) (: (cefl $fp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊥)))
             (empty)))
       (empty)))

;; Conjunction evaluation to ⊥, right: the right conjunct is false of $x.
;; `cefr` stands for conjunction evaluates to false from right.
(= (nupln_bc_nf $kb $budget (: (cefr $gp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊥)))
   (if (<= 3 $budget)
       (let* (((MkSized $ps (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊥)))
               (nupln_bc_nf $kb (- $budget 2) (: $gp (≞ (→ (⊙ $x) (lam $v $g)) ⊥))))
              ((MkSized $as (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))
               (nupln_bc_nf $kb (- $budget (+ $ps 1)) (: (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) $a))))
         (if (nf:ordered $kb $p $g)
             (MkSized (+ 1 (+ $as $ps)) (: (cefr $gp) (≞ (→ (⊙ $x) (lam $v (∧ ($p $v) $g))) ⊥)))
             (empty)))
       (empty)))

;; Evaluation of a predicate applied to the bound variable (η): the evidence
;; for the predicate itself at $x.
(= (nupln_bc_nf $kb $budget (: $pp (≞ (→ (⊙ $x) (lam $v ($p $v))) $tv)))
   (if (<= 1 $budget)
       (match $kb (: $pp (≞ (→ (⊙ $x) $p) $tv))
         (MkSized 1 (: $pp (≞ (→ (⊙ $x) (lam $v ($p $v))) $tv))))
       (empty)))

;; Product.  Both $a and $b must be proven for (, $a $b) to be proven.
(= (nupln_bc_nf $kb $budget (: (, $x $y) (, $a $b)))
   (if (<= 3 $budget)
       (let* (((MkSized $xs (: $x $a))
               (nupln_bc_nf $kb (- $budget 2) (: $x $a)))
              ((MkSized $ys (: $y $b))
               (nupln_bc_nf $kb (- $budget (+ $xs 1)) (: $y $b))))
         (MkSized (+ 1 (+ $xs $ys)) (: (, $x $y) (, $a $b))))
       (empty)))

;; A lambda shown with its bound variable named x.
(= (readable (lam $v $body)) (let $v x (lam x $body)))

;; The normal form accepts (∧ F G) with its usual size, and refuses the
;; mirror and the repetition.
!(test
  (nupln_bc_nf &kb 3 (: (lam $x (∧ (F $x) (G $x))) $a))
  (MkSized 3 (: (lam $x (∧ (F $x) (G $x))) (-> UInt Bool))))
!(test (collapse (nupln_bc_nf &kb 3 (: (lam $x (∧ (G $x) (F $x))) $a))) ())
!(test (collapse (nupln_bc_nf &kb 3 (: (lam $x (∧ (F $x) (F $x))) $a))) ())

;; Every hypothesis that explains the truth table, first without and then
;; with normal forms.  Without them the mirror image is a second answer.
!(WithoutNormalForms
   (collapse
     (let (MkSized $n (: $prf (, (≞ (→ (⊙ 0) $h) ⊤) $rest)))
          (nupln_bc &kb 27 (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤)
                                   (, (≞ (→ (⊙ 1) $pred) ⊥)
                                      (≞ (→ (⊙ 2) $pred) ⊥)))))
       (readable $h))))
!(WithNormalForms
   (collapse
     (let (MkSized $n (: $prf (, (≞ (→ (⊙ 0) $h) ⊤) $rest)))
          (nupln_bc_nf &kb 27 (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤)
                                      (, (≞ (→ (⊙ 1) $pred) ⊥)
                                         (≞ (→ (⊙ 2) $pred) ⊥)))))
       (readable $h))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Beyond the combinator version: the learned predicate is a function ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Learn again, keep the hypothesis, and show it with its bound variable
;; named x.
!(let (MkSized $size (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤) $rest)))
      (once (nupln_bc &kb 27 (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤)
                                     (, (≞ (→ (⊙ 1) $pred) ⊥)
                                        (≞ (→ (⊙ 2) $pred) ⊥))))))
   (let $stored (add-atom &self (learned $pred))
     (let (lam $v $body) $pred
       (let $v x (Learned (lam x $body))))))

;; Give F, G and ∧ executable definitions, beyond the three observed values,
;; and call the learned function.  It is applied like any function; nothing is
;; translated back from combinators.
(= (F $x) (== (% $x 2) 0))
(= (G $x) (< $x 2))
(= (∧ $a $b) (and $a $b))

!(match &self (learned $h)
   (Run (collapse (let $x (superpose (0 1 2 3 4)) (At $x ($h $x))))))

; Equations on the kernel (megalodon_equations): the laws of equality proved
; in the theory, conversions as data, rewriting at one stated position, and
; the two tactics.  The theory is built here: equality as Megalodon defines
; it, a zero, a successor, an addition with its two equations as axioms, and
; a property "small" for the rules with a premise.  Each concept is shown
; where it holds and where it does not.
!(import! &self megalodon)
!(import! &self (library megalodon_equations))
!(let $state (mg:new-theory-state custom) (bind! &th $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
(= (C0) (mg:empty-context))
(= (E) (Equality (T) "eq"))
(= (base) (Tp.base 0))
(= (zero) (Tm.named "zero"))
(= (suc $x) (Tm.app (Tm.named "suc") $x))
(= (one) (suc (zero)))
(= (two) (suc (one)))
(= (add $a $b) (Tm.app (Tm.app (Tm.named "add") $a) $b))
(= (pair $a $b) (Tm.app (Tm.app (Tm.named "pair") $a) $b))
(= (small $a) (Tm.app (Tm.named "small") $a))
; whether THEOREM states A = B up to the kernel's conversion
(= (states $theorem $a $b) (mg:try-convertible (T) (C0) (mg:proposition $theorem) (eq:of (E) $a $b)))
(= (proves $theorem $p) (mg:try-convertible (T) (C0) (mg:proposition $theorem) $p))

!(mg:define (T) "eq" (Tp.all (Tp.arr (Tp.var 0) (Tp.arr (Tp.var 0) (Tp.prop))))
   (Tm.typeLam (Tm.lam (Tp.var 0) (Tm.lam (Tp.var 0)
     (Tm.all (Tp.arr (Tp.var 0) (Tp.arr (Tp.var 0) (Tp.prop)))
       (Tm.imp (Tm.app (Tm.app (Tm.db 0) (Tm.db 2)) (Tm.db 1)) (Tm.app (Tm.app (Tm.db 0) (Tm.db 1)) (Tm.db 2))))))))
!(mg:parameter (AUTH) "zero" (base))
!(mg:parameter (AUTH) "suc" (Tp.arr (base) (base)))
!(mg:parameter (AUTH) "add" (Tp.arr (base) (Tp.arr (base) (base))))
!(mg:parameter (AUTH) "pair" (Tp.arr (base) (Tp.arr (base) (base))))
!(mg:parameter (AUTH) "small" (Tp.arr (base) (Tp.prop)))
!(mg:parameter (AUTH) "c" (base))
!(mg:axiom (AUTH) "add_zero" (Tm.all (base) (eq:of (E) (add (Tm.db 0) (zero)) (Tm.db 0))))
!(mg:axiom (AUTH) "add_suc"
   (Tm.all (base) (Tm.all (base) (eq:of (E) (add (Tm.db 1) (suc (Tm.db 0))) (suc (add (Tm.db 1) (Tm.db 0)))))))
!(mg:axiom (AUTH) "small_zero" (small (zero)))
!(mg:axiom (AUTH) "small_suc" (Tm.all (base) (Tm.imp (small (Tm.db 0)) (small (suc (Tm.db 0))))))
!(mg:axiom (AUTH) "zero_add_small" (Tm.all (base) (Tm.imp (small (Tm.db 0)) (eq:of (E) (add (zero) (Tm.db 0)) (Tm.db 0)))))

; 1. the laws, proved once from the definition of equality
!(eq:init (E))
!(eq:init (E))
!(LAW refl (states (eq:refl (E) (C0) (two)) (two) (two)))
!(let $e (mg:term-app (mg:known-in (T) (C0) "add_zero") (two))
   (LAW sym (states (eq:sym (E) (C0) (add (two) (zero)) (two) $e) (two) (add (two) (zero)))))
!(let $e (mg:term-app (mg:known-in (T) (C0) "add_zero") (two))
   (LAW cong (states (eq:cong (E) (C0) (Tm.lam (base) (suc (Tm.db 0))) (add (two) (zero)) (two) $e)
                     (suc (add (two) (zero))) (suc (two)))))
; the kernel does not convert add two zero to two by itself: that is a theorem
!(LAW not-a-conversion (mg:try-convertible (T) (C0) (add (two) (zero)) (two)))
; the kernel refuses symmetry applied to an equation between other terms
!(let $e (mg:term-app (mg:known-in (T) (C0) "add_zero") (two))
   (case (mg:try-proof-app (mg:term-app (mg:term-app (mg:known-in (T) (C0) "megalodon_eq_sym") (one)) (two)) $e)
     (((Error $call $why) (LAW other-equation refused))
      ($theorem (LAW other-equation accepted)))))

; 2. a rule wherever it applies, to the end
!(let $t (add (suc (add (two) (zero))) (zero))
   (case (conv:run (E) (C0) (Cv.repeat (Cv.top-down (Cv.rule "add_zero" Prover.none))) $t)
     (((Conv $t2 $theorem) (RULE repeat (== $t2 (suc (two))) (states $theorem $t (suc (two)))))
      ($other (RULE repeat $other)))))
; two rules: add one two computed to three
!(let $t (add (one) (two))
   (case (conv:run (E) (C0) (Cv.repeat (Cv.top-down (Cv.orelse (Cv.rule "add_suc" Prover.none) (Cv.rule "add_zero" Prover.none)))) $t)
     (((Conv $t2 $theorem) (RULE compute (== $t2 (suc (two))) (states $theorem $t (suc (two)))))
      ($other (RULE compute $other)))))
; the first position may lie deep: inside the successor inside the pair
!(let $t (pair (suc (add (one) (zero))) (zero))
   (case (conv:run (E) (C0) (Cv.top-down (Cv.rule "add_zero" Prover.none)) $t)
     (((Conv $t2 $theorem) (RULE deep (== $t2 (pair (suc (one)) (zero))) (states $theorem $t (pair (suc (one)) (zero)))))
      ($other (RULE deep $other)))))
; a term no rule applies to is left as it is
!(RULE none (conv:run (E) (C0) (Cv.repeat (Cv.top-down (Cv.rule "add_zero" Prover.none))) (suc (two))))

; 3. one position only: the argument of the pair, its other copy untouched
!(let $t (pair (add (one) (zero)) (add (one) (zero)))
   (case (conv:run (E) (C0) (Cv.path (arg) (Cv.rule "add_zero" Prover.none)) $t)
     (((Conv $t2 $theorem)
       (POSITION (== $t2 (pair (add (one) (zero)) (one)))
                 (states $theorem $t (pair (add (one) (zero)) (one)))
                 ; and the answer does not state that both copies changed
                 (states $theorem $t (pair (one) (one)))))
      ($other (POSITION $other)))))
; a path that leads nowhere leaves the term as it is
!(POSITION nowhere (conv:run (E) (C0) (Cv.path (arg arg arg arg) (Cv.rule "add_zero" Prover.none)) (pair (zero) (zero))))

; 4. a rule with a premise applies where the prover proves the premise
!(case (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.known "small_zero")) (add (zero) (zero)))
   (((Conv $t2 $theorem) (PREMISE known (== $t2 (zero)) (states $theorem (add (zero) (zero)) (zero))))
    ($other (PREMISE known $other))))
; the same prover does not prove that two is small, and nothing proves it with no prover
!(PREMISE known-other (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.known "small_zero")) (add (zero) (two))))
!(PREMISE none (conv:run (E) (C0) (Cv.rule "zero_add_small" Prover.none) (add (zero) (zero))))
; backchaining through the two facts about small proves it for two
!(case (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.horn (lemmas "small_zero" "small_suc") 4)) (add (zero) (two)))
   (((Conv $t2 $theorem) (PREMISE horn (== $t2 (two)) (states $theorem (add (zero) (two)) (two))))
    ($other (PREMISE horn $other))))
; and not when it may go only one lemma deep
!(PREMISE horn-shallow (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.horn (lemmas "small_zero" "small_suc") 1)) (add (zero) (two))))

; a declared rule set, offered by the head of the term; values first:
; add (add zero two) (add one one) is four
!(conv:declare (E) (C0) arithmetic "add_zero")
!(conv:declare (E) (C0) arithmetic "add_suc")
!(let $t (add (add (zero) (two)) (add (one) (one)))
   (case (conv:run (E) (C0) (Cv.innermost (Cv.rules arithmetic Prover.none None)) $t)
     (((Conv $t2 $theorem) (SET innermost (== $t2 (suc (suc (two)))) (states $theorem $t (suc (suc (two))))))
      ($other (SET innermost $other)))))
; a set whose rule has a premise: used where the prover proves it, not otherwise
!(conv:declare (E) (C0) left "zero_add_small")
!(case (conv:run (E) (C0) (Cv.rules left (Prover.horn (lemmas "small_zero" "small_suc") 4) None) (add (zero) (two)))
   (((Conv $t2 $theorem) (SET premise (== $t2 (two)) (states $theorem (add (zero) (two)) (two))))
    ($other (SET premise $other))))
!(SET premise-unproved (conv:run (E) (C0) (Cv.rules left Prover.none None) (add (zero) (two))))
; a set with no rule for the head leaves the term as it is, and so does an undeclared set
!(SET no-rule (conv:run (E) (C0) (Cv.innermost (Cv.rules arithmetic Prover.none None)) (pair (zero) (zero)))
              (conv:run (E) (C0) (Cv.innermost (Cv.rules nothing Prover.none None)) (add (zero) (zero))))
; a known that is not an equation is not a rule
!(case (conv:declare (E) (C0) arithmetic "small_zero") (((Error $call $why) (SET not-an-equation refused)) ($head (SET not-an-equation $head))))

; a goal with binders is proved under them: a function is typed by typing
; its body at a variable of its domain (membership, functions as sets and
; their spaces are parameters here, with the typing facts as axioms)
!(mg:parameter (AUTH) "N" (base))
!(mg:parameter (AUTH) "member" (Tp.arr (base) (Tp.arr (base) (Tp.prop))))
!(mg:parameter (AUTH) "fn" (Tp.arr (base) (Tp.arr (Tp.arr (base) (base)) (base))))
!(mg:parameter (AUTH) "space" (Tp.arr (base) (Tp.arr (Tp.arr (base) (base)) (base))))
(= (is-in $a $A) (Tm.app (Tm.app (Tm.named "member") $a) $A))
(= (fn-of $A $f) (Tm.app (Tm.app (Tm.named "fn") $A) $f))
(= (fn-space $A $B) (Tm.app (Tm.app (Tm.named "space") $A) $B))
!(mg:axiom (AUTH) "zero_in" (is-in (zero) (Tm.named "N")))
!(mg:axiom (AUTH) "suc_in" (Tm.all (base) (Tm.imp (is-in (Tm.db 0) (Tm.named "N")) (is-in (suc (Tm.db 0)) (Tm.named "N")))))
!(mg:axiom (AUTH) "fn_in"
   (Tm.all (base) (Tm.all (Tp.arr (base) (base)) (Tm.all (Tp.arr (base) (base))
     (Tm.imp (Tm.all (base) (Tm.imp (is-in (Tm.db 0) (Tm.db 3)) (is-in (Tm.app (Tm.db 1) (Tm.db 0)) (Tm.app (Tm.db 2) (Tm.db 0)))))
             (is-in (fn-of (Tm.db 2) (Tm.db 0)) (fn-space (Tm.db 2) (Tm.db 1))))))))
!(let $goal (is-in (fn-of (Tm.named "N") (Tm.lam (base) (suc (suc (Tm.db 0))))) (fn-space (Tm.named "N") (Tm.lam (base) (Tm.named "N"))))
   (case (collapse (conv:prove (Prover.horn (lemmas "zero_in" "suc_in" "fn_in") 6) (E) (C0) $goal))
     ((() (TYPING function unproved))
      ($found (TYPING function (proves (car-atom $found) $goal))))))
; not without the fact about functions, and not for a body outside the space
!(TYPING no-function-fact
   (collapse (conv:prove (Prover.horn (lemmas "zero_in" "suc_in") 6) (E) (C0)
               (is-in (fn-of (Tm.named "N") (Tm.lam (base) (suc (suc (Tm.db 0))))) (fn-space (Tm.named "N") (Tm.lam (base) (Tm.named "N")))))))
!(TYPING wrong-body
   (collapse (conv:prove (Prover.horn (lemmas "zero_in" "suc_in" "fn_in") 6) (E) (C0)
               (is-in (fn-of (Tm.named "N") (Tm.lam (base) (pair (Tm.db 0) (Tm.db 0)))) (fn-space (Tm.named "N") (Tm.lam (base) (Tm.named "N")))))))

; 5. an equation in hand is used only at a term convertible with its left side
!(let $e (mg:term-app (mg:known-in (T) (C0) "add_zero") (two))
   (EQUATION (case (conv:run (E) (C0) (Cv.equation $e) (add (two) (zero)))
               (((Conv $t2 $theorem) (== $t2 (two))) ($other $other)))
             (conv:run (E) (C0) (Cv.equation $e) (add (one) (zero)))))

; 6. answers are kept: the second run is found, not made
(= (kept) (collapse (match &self (ConvFact $E $ctx $c $t $answer) $t)))
!(let* (($before (kept))
        ($first (conv:run (E) (C0) (Cv.memo (Cv.rule "add_zero" Prover.none)) (add (one) (zero))))
        ($once (kept))
        ($second (conv:run (E) (C0) (Cv.memo (Cv.rule "add_zero" Prover.none)) (add (one) (zero))))
        ($twice (kept)))
   (MEMO (== $before ()) (== $once ((add (one) (zero)))) (== $twice $once)
         (case $second (((Conv $t2 $theorem) (states $theorem (add (one) (zero)) (one))) ($other $other)))))

; 7. the tactics: convert a subterm of the goal, prove what results
!(let $goal (small (add (zero) (zero)))
   (TACTIC conv (proves (mg:run (Tac.conv "eq" (arg) (Cv.rule "add_zero" Prover.none) (Tac.exact (mg:known-in (T) (C0) "small_zero")))
                                (T) (C0) $goal)
                        $goal)))
; no answer when what results is not proved
!(TACTIC conv-unproved
   (collapse (mg:run (Tac.conv "eq" (arg) (Cv.rule "add_zero" Prover.none) (Tac.exact (mg:known-in (T) (C0) "small_zero")))
                     (T) (C0) (small (add (one) (zero))))))
; rewrite by a hypothesis: c = zero -> small zero -> small c
!(let $goal (Tm.imp (eq:of (E) (Tm.named "c") (zero)) (Tm.imp (small (zero)) (small (Tm.named "c"))))
   (TACTIC rewrite (proves (mg:run (Tac.intros (Tac.rewrite "eq" (From.hyp 1) (arg) Tac.assumption)) (T) (C0) $goal) $goal)))
; no answer when the equation is not about the subterm at the path
!(TACTIC rewrite-elsewhere
   (collapse (mg:run (Tac.intros (Tac.rewrite "eq" (From.hyp 1) (fun) Tac.assumption)) (T) (C0)
                     (Tm.imp (eq:of (E) (Tm.named "c") (zero)) (Tm.imp (small (zero)) (small (Tm.named "c")))))))

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

; 8. views.  bit is a value with a view, bit = suc zero.  The rules of
; addition ask for zero or a successor in the second place, so add zero bit
; is computed after the view
!(mg:parameter (AUTH) "bit" (base))
!(mg:parameter (AUTH) "app0" (Tp.arr (Tp.arr (base) (base)) (base)))
!(mg:axiom (AUTH) "bit_view" (eq:of (E) (Tm.named "bit") (suc (zero))))
!(mg:axiom (AUTH) "pair_both"
   (Tm.all (base) (Tm.all (base) (eq:of (E) (pair (suc (Tm.db 1)) (suc (Tm.db 0))) (pair (Tm.db 1) (Tm.db 0))))))
!(mg:axiom (AUTH) "pair_swap"
   (Tm.all (base) (Tm.all (base) (eq:of (E) (pair (Tm.db 1) (Tm.db 0)) (pair (Tm.db 0) (Tm.db 1))))))
!(mg:axiom (AUTH) "pair_same" (Tm.all (base) (eq:of (E) (pair (Tm.db 0) (Tm.db 0)) (pair (Tm.db 0) (Tm.db 0)))))
!(conv:declare (E) (C0) views "bit_view")
(= (shown) (Cv.rules views Prover.none None))
!(conv:declare (E) (C0) both "pair_both")
!(let $t (add (zero) (Tm.named "bit"))
   (case (conv:run (E) (C0) (Cv.viewed arithmetic (shown) Prover.none None) $t)
     (((Conv $t2 $theorem) (VIEW used (== $t2 (suc (add (zero) (zero)))) (states $theorem $t (suc (add (zero) (zero))))))
      ($other (VIEW used $other)))))
; the plain rules leave it as it is
!(VIEW plain-rules (conv:run (E) (C0) (Cv.rules arithmetic Prover.none None) (add (zero) (Tm.named "bit"))))
; a view is not a rule: where no rule asks for the form it shows, the value stays
!(VIEW not-asked (conv:run (E) (C0) (Cv.viewed arithmetic (shown) Prover.none None) (pair (Tm.named "bit") (zero))))
; and it is kept only where the rule then applies: pair_both wants two
; successors, the view gives the first, zero is not the second
!(VIEW rule-still-fails (conv:run (E) (C0) (Cv.viewed both (shown) Prover.none None) (pair (Tm.named "bit") (zero))))
!(let $t (pair (Tm.named "bit") (Tm.named "bit"))
   (case (conv:run (E) (C0) (Cv.viewed both (shown) Prover.none None) $t)
     (((Conv $t2 $theorem) (VIEW twice (== $t2 (pair (zero) (zero))) (states $theorem $t (pair (zero) (zero)))))
      ($other (VIEW twice $other)))))
; where the pattern and the term part: the place, nothing, or a difference in length
!(MISMATCH place (mgt:mismatch (add (Tm.db 0) (suc (Tm.db 1))) (add (zero) (Tm.named "bit")) ()))
!(MISMATCH none (mgt:mismatch (add (Tm.db 0) (suc (Tm.db 1))) (add (zero) (suc (zero))) ()))
!(MISMATCH shorter (mgt:mismatch (add (Tm.db 0) (suc (Tm.db 1))) (Tm.app (Tm.named "add") (zero)) ()))

; 9. an instance that changes nothing is no step, and a rule that can change
; nothing is refused
!(case (conv:run (E) (C0) (Cv.rule "pair_swap" Prover.none) (pair (zero) (one)))
   (((Conv $t2 $theorem) (IDENTITY swap (== $t2 (pair (one) (zero)))))
    ($other (IDENTITY swap $other))))
!(IDENTITY swap-equal-parts (conv:run (E) (C0) (Cv.rule "pair_swap" Prover.none) (pair (one) (one))))
!(IDENTITY declared (case (conv:declare (E) (C0) same "pair_same") (((Error $call $why) refused) ($head $head))))

; 10. an equation turned around, proved again in the theory
!(case (eq:turned (E) "add_zero" "add_zero_turned")
   (((Error $call $why) (TURNED plain refused))
    ($theorem (TURNED plain (proves $theorem (Tm.all (base) (eq:of (E) (Tm.db 0) (add (Tm.db 0) (zero)))))))))
!(case (eq:turned (E) "zero_add_small" "zero_add_small_turned")
   (((Error $call $why) (TURNED premise refused))
    ($theorem (TURNED premise
       (proves $theorem (Tm.all (base) (Tm.imp (small (Tm.db 0)) (eq:of (E) (Tm.db 0) (add (zero) (Tm.db 0))))))))))
; the turned equation is a rule like any other: zero is add zero zero
!(case (conv:run (E) (C0) (Cv.rule "add_zero_turned" Prover.none) (zero))
   (((Conv $t2 $theorem) (TURNED as-rule (== $t2 (add (zero) (zero)))))
    ($other (TURNED as-rule $other))))
; a known that states no equation has nothing to turn
!(TURNED not-an-equation (case (eq:turned (E) "small_zero" "small_zero_turned") (((Error $call $why) refused) ($theorem proved))))

; 11. evaluation: values first, each answer kept
(= (evaluated) (collapse (match &self (ConvFact $e $c (Cv.eval $i) $t $answer) $t)))
(= (arith) (Cv.eval (Cv.rules arithmetic Prover.none None)))
!(let* (($t (add (add (zero) (two)) (add (one) (one))))
        ($first (conv:run (E) (C0) (arith) $t))
        ($once (evaluated))
        ($second (conv:run (E) (C0) (arith) $t))
        ($twice (evaluated)))
   (EVAL value
     (case $first (((Conv $t2 $theorem) (states $theorem $t (suc (suc (two))))) ($other $other)))
     (case $second (((Conv $t3 $again) (and (== $t3 (suc (suc (two)))) (states $again $t (suc (suc (two)))))) ($other $other)))
     (== $once $twice)))
; what a value was computed from is among the kept answers
(= (came-from $term $origins)
   (if (== $origins ()) False
       (case (car-atom $origins) (((Origin $s $theorem) (if (== $s $term) True (came-from $term (cdr-atom $origins)))))))) 
!(EVAL origin (came-from (add (zero) (two)) (conv:origins (E) (C0) (two))))
; with its own value and no other: add one zero was computed too, to one
!(EVAL origin-other (came-from (add (one) (zero)) (conv:origins (E) (C0) (two))))
; and a value that nothing was computed to has no origin
!(EVAL origin-none (conv:origins (E) (C0) (pair (two) (two))))
; a value is its own value
!(EVAL of-a-value (conv:run (E) (C0) (arith) (suc (two))))
; a function given some of its arguments: those are evaluated in place
!(let $t (Tm.app (Tm.named "app0") (Tm.app (Tm.named "add") (add (zero) (zero))))
   (case (conv:run (E) (C0) (arith) $t)
     (((Conv $t2 $theorem)
       (EVAL in-place (== $t2 (Tm.app (Tm.named "app0") (Tm.app (Tm.named "add") (zero))))
             (states $theorem $t (Tm.app (Tm.named "app0") (Tm.app (Tm.named "add") (zero))))))
      ($other (EVAL in-place $other)))))
; the body of a binder is left as it is
!(EVAL under-binder (conv:run (E) (C0) (arith) (Tm.app (Tm.named "app0") (Tm.lam (base) (add (Tm.db 0) (zero))))))
!(EVAL whole (mgt:whole (E) (add (zero) (zero))) (mgt:whole (E) (Tm.app (Tm.named "add") (zero))) (mgt:whole (E) (small (zero))))

; 12. a file's own conversion and prover, and the small term utilities
(= (conv:run-with (Cv.to-zero) $E $ctx $t)
   (if (== $t (add (zero) (zero))) (conv:run $E $ctx (Cv.rule "add_zero" Prover.none) $t) Same))
(= (conv:prove-with (Prover.small-zero) $E $ctx $p) (mg:exact (eq:theory $E) $ctx $p (mg:known-in (eq:theory $E) $ctx "small_zero")))
; its witness for a quantifier the left side leaves open: zero
(= (conv:witness-with (Prover.small-zero) $E $ctx $body $k $binds) (zero))
!(case (conv:run (E) (C0) (Cv.to-zero) (add (zero) (zero)))
   (((Conv $t2 $theorem) (OWN conversion (== $t2 (zero))))
    ($other (OWN conversion $other))))
!(OWN conversion-elsewhere (conv:run (E) (C0) (Cv.to-zero) (add (one) (zero))))
!(case (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.small-zero)) (add (zero) (zero)))
   (((Conv $t2 $theorem) (OWN prover (== $t2 (zero))))
    ($other (OWN prover $other))))
!(OWN prover-unproved (conv:run (E) (C0) (Cv.rule "zero_add_small" (Prover.small-zero)) (add (zero) (two))))
!(PARTS (mgt:spine-args (add (zero) (one))) (mgt:nth (mgt:spine-args (add (zero) (one))) 1) (mgt:nth () 0)
        (mgt:position (one) (mgt:spine-args (add (zero) (one))) 0) (mgt:position (two) (mgt:spine-args (add (zero) (one))) 0))

; 13. evaluation by need.  pick chooses by its first argument; loop is a
; constant that rewrites to its own successor for ever.  Computing every
; argument first would never return from pick zero .. loop
!(mg:parameter (AUTH) "pick" (Tp.arr (base) (Tp.arr (base) (Tp.arr (base) (base)))))
!(mg:parameter (AUTH) "loop" (base))
!(mg:parameter (AUTH) "twin" (Tp.arr (base) (Tp.arr (base) (base))))
(= (pick $c $a $b) (Tm.app (Tm.app (Tm.app (Tm.named "pick") $c) $a) $b))
(= (twin $a $b) (Tm.app (Tm.app (Tm.named "twin") $a) $b))
!(mg:axiom (AUTH) "pick_zero" (Tm.all (base) (Tm.all (base) (eq:of (E) (pick (zero) (Tm.db 1) (Tm.db 0)) (Tm.db 1)))))
!(mg:axiom (AUTH) "pick_suc"
   (Tm.all (base) (Tm.all (base) (Tm.all (base) (eq:of (E) (pick (suc (Tm.db 2)) (Tm.db 1) (Tm.db 0)) (Tm.db 0))))))
!(mg:axiom (AUTH) "loop_step" (eq:of (E) (Tm.named "loop") (suc (Tm.named "loop"))))
!(mg:axiom (AUTH) "twin_same" (Tm.all (base) (eq:of (E) (twin (Tm.db 0) (Tm.db 0)) (zero))))
!(conv:declare (E) (C0) choice "pick_zero")
!(conv:declare (E) (C0) choice "pick_suc")
!(conv:declare (E) (C0) choice "loop_step")
!(conv:declare (E) (C0) choice "twin_same")
!(conv:declare (E) (C0) choice "add_zero")
!(conv:declare (E) (C0) choice "add_suc")
(= (by-need) (Cv.eval-needed choice (Cv.rules choice Prover.none None)))
; what each head needs first: pick its first argument, twin both (its rule
; has one variable twice), a head with no rule all of them
!(NEED flags (conv:needed (E) (C0) choice (Tm.named "pick")) (conv:needed (E) (C0) choice (Tm.named "twin"))
        (conv:needed (E) (C0) choice (Tm.named "pair")))
!(let $t (pick (zero) (add (zero) (zero)) (Tm.named "loop"))
   (case (conv:run (E) (C0) (by-need) $t)
     (((Conv $t2 $theorem) (NEED branch-taken (== $t2 (zero)) (states $theorem $t (zero))))
      ($other (NEED branch-taken $other)))))
; the branch not taken was never computed
!(let $untaken (Tm.named "loop")
   (NEED branch-not-taken (collapse (match &self (ConvFact $e $c (Cv.eval-needed $s $i) $untaken $answer) kept))))
!(let $t (pick (suc (zero)) (Tm.named "loop") (add (zero) (zero)))
   (case (conv:run (E) (C0) (by-need) $t)
     (((Conv $t2 $theorem) (NEED other-branch (== $t2 (zero))))
      ($other (NEED other-branch $other)))))
; the argument a rule needs is computed first: the condition add zero zero
!(let $t (pick (add (zero) (zero)) (one) (Tm.named "loop"))
   (case (conv:run (E) (C0) (by-need) $t)
     (((Conv $t2 $theorem) (NEED condition-first (== $t2 (one)) (states $theorem $t (one))))
      ($other (NEED condition-first $other)))))
; a rule with one variable twice compares values: both arguments first
!(let $t (twin (add (zero) (zero)) (zero))
   (case (conv:run (E) (C0) (by-need) $t)
     (((Conv $t2 $theorem) (NEED same-values (== $t2 (zero))))
      ($other (NEED same-values $other)))))
; where no rule applies, the arguments that waited are computed after all
!(let $t (pick (Tm.named "c") (add (zero) (zero)) (zero))
   (case (conv:run (E) (C0) (by-need) $t)
     (((Conv $t2 $theorem) (NEED no-rule (== $t2 (pick (Tm.named "c") (zero) (zero))) (states $theorem $t (pick (Tm.named "c") (zero) (zero)))))
      ($other (NEED no-rule $other)))))
; a head declared strict takes all its arguments computed
!(let $done (conv:declare-strict choice (Tm.named "small")) (NEED strict (conv:needed (E) (C0) choice (Tm.named "small"))))

; 14. evidence in a space of its own: the answers kept from here on do not
; enter the program's space, and are found again all the same
!(bind! &evidence (new-space))
(= (count-of $xs) (if (== $xs ()) 0 (+ 1 (count-of (cdr-atom $xs)))))
(= (in-program) (count-of (collapse (match &self (ConvFact $e $c $i $t $answer) $t))))
!(let* (($before (in-program))
        ($given (conv:keep-evidence-in &evidence))
        ($t (add (add (one) (one)) (two)))
        ($first (conv:run (E) (C0) (arith) $t))
        ($after (in-program))
        ($second (conv:run (E) (C0) (arith) $t)))
   (EVIDENCE apart
     (case $first (((Conv $t2 $theorem) (states $theorem $t (suc (suc (two))))) ($other $other)))
     (== $before $after)
     (case $second (((Conv $t3 $again) (states $again $t (suc (suc (two))))) ($other $other)))
     (not (== (collapse (match &evidence (ConvFact $e $c $i $t $answer) kept)) ()))))
; a kept theorem is a known of the theory under a name of its own, and the
; row holds the name: no row holds a theorem
!(let $t (add (add (one) (one)) (two))
   (EVIDENCE named (collapse (match &evidence (ConvFact $e $c $i $t (Conv $value (Named $name))) (proves (mg:known-in (T) (C0) $name) (eq:of (E) $t (suc (suc (two)))))))))
!(EVIDENCE none-held (collapse (match &evidence (ConvFact $e $c $i $t (Conv $value (Held $theorem))) held)))
; where the value came from is asked the same way as before
!(EVIDENCE origin (came-from (add (add (one) (one)) (two)) (conv:origins (E) (C0) (suc (suc (two))))))
; a term evaluated before the space was given is not in it
!(let $earlier (add (add (zero) (two)) (add (one) (one)))
   (EVIDENCE earlier-not-moved (collapse (match &evidence (ConvFact $e $c $i $earlier $answer) kept))))

; 15. no rule applies, or none could be decided: the difference is kept, with
; its reason.  add zero x = x needs x small.  With no prover its premise is
; open at add zero two: the term is left, and the evidence names the rule and
; the premise
!(conv:declare (E) (C0) guarded "zero_add_small")
!(UNDECIDED left (conv:run (E) (C0) (Cv.rules guarded Prover.none None) (add (zero) (two))))
!(let* (($t (add (zero) (two))) ($premise (small (two))))
   (UNDECIDED reason (collapse (match &evidence (ConvUndecided $e $c $t $name $why) ($name (== $why (Open $premise)))))))
; a term no left side matches is left too, and nothing is recorded for it
!(UNDECIDED no-match (conv:run (E) (C0) (Cv.rules guarded Prover.none None) (add (one) (two))))
!(let $t (add (one) (two))
   (UNDECIDED no-match-recorded (collapse (match &evidence (ConvUndecided $e $c $t $name $why) $name))))
; with a prover for the premise the rule is decided
!(case (conv:run (E) (C0) (Cv.rules guarded (Prover.horn (lemmas "small_zero" "small_suc") 4) None) (add (zero) (two)))
   (((Conv $t2 $theorem) (UNDECIDED proved (== $t2 (two))))
    ($other (UNDECIDED proved $other))))
; a later rule that applies is used, and then nothing is undecided at the term
!(conv:declare (E) (C0) guarded2 "zero_add_small")
!(conv:declare (E) (C0) guarded2 "add_suc")
!(let $t (add (zero) (one))
   (case (conv:run (E) (C0) (Cv.rules guarded2 Prover.none None) $t)
     (((Conv $t2 $theorem)
       (UNDECIDED later-rule (== $t2 (suc (add (zero) (zero))))
                  (collapse (match &evidence (ConvUndecided $e $c $t $name $why) $name))))
      ($other (UNDECIDED later-rule $other)))))
; another reason: the instance cannot be stated.  add zero x = x for any
; small y: the left side does not say which y
!(mg:axiom (AUTH) "zero_add_witness"
   (Tm.all (base) (Tm.all (base) (Tm.imp (small (Tm.db 0)) (eq:of (E) (add (zero) (Tm.db 1)) (Tm.db 1))))))
!(conv:declare (E) (C0) witness "zero_add_witness")
!(UNDECIDED unstated (conv:run (E) (C0) (Cv.rules witness (Prover.known "small_zero") None) (add (zero) (one))))
!(let $t (add (zero) (one))
   (UNDECIDED unstated-reason (collapse (match &evidence (ConvUndecided $e $c $t "zero_add_witness" $why) $why))))
; given a witness it is stated and proved: as a default of the conversion,
; or found by the prover (the file's own prover of section 12 answers zero)
!(case (conv:run (E) (C0) (Cv.rules witness (Prover.known "small_zero") (zero)) (add (zero) (one)))
   (((Conv $t2 $theorem) (UNDECIDED witness-given (== $t2 (one))))
    ($other (UNDECIDED witness-given $other))))
!(case (conv:run (E) (C0) (Cv.rules witness (Prover.small-zero) None) (add (zero) (two)))
   (((Conv $t2 $theorem) (UNDECIDED witness-found (== $t2 (two))))
    ($other (UNDECIDED witness-found $other))))

; 16. an open premise is an open goal, and an open goal is a hypothesis.
; The computation that stopped at "two is small" is run again in a context
; that assumes it, and what it proves is closed over the assumption:
; small two -> add zero two = two.  That theorem depends on the premise and
; on nothing else
!(let* (($t (add (zero) (two)))
        ($premise (small (two)))
        ($open (conv:open-premises (E) (C0)))
        ($ctx (conv:assuming (E) (C0) $open))
        ($under (conv:run (E) $ctx (Cv.rules guarded (Prover.tactic Tac.assumption) None) $t))
        ($closed (conv:close-over (E) $under $open)))
   (OPEN goal (== $open ($premise))
     (case $closed
       (((Conv $t2 $theorem)
         (OPEN-THEOREM (== $t2 (two)) (proves $theorem (Tm.imp $premise (eq:of (E) $t (two)))) (states $theorem $t (two))))
        ($other $other)))))
; nothing is open where everything was decided
!(OPEN none (conv:open-premises (E) (conv:assuming (E) (C0) ((small (zero))))))
; a theorem that was not made under a premise is not closed over it
!(let* (($premise (small (two))) ($plain (eq:refl (E) (C0) (two))))
   (OPEN not-assumed (conv:close-over (E) (Conv (two) $plain) ($premise))))
!(UNDECIDED listed
   (case (conv:undecided (E) (C0))
     ((() none)
      ($rows (case (car-atom $rows) (((Undecided $term $name $why) (== $name "zero_add_small")) ($other $other)))))))

; 17. under a binder.  bind X F is a binder form (think of the product of a
; family), inb its membership, and its family lemma says: F x = G x for every
; x in X gives bind X F = bind X G.  The kernel alone does not rewrite
; add x zero to x inside the function; the lemma carries the equation in
!(mg:parameter (AUTH) "bind" (Tp.arr (base) (Tp.arr (Tp.arr (base) (base)) (base))))
!(mg:parameter (AUTH) "inb" (Tp.arr (base) (Tp.arr (base) (Tp.prop))))
(= (bind $X $F) (Tm.app (Tm.app (Tm.named "bind") $X) $F))
!(mg:axiom (AUTH) "bind_family"
   (Tm.all (base) (Tm.all (Tp.arr (base) (base)) (Tm.all (Tp.arr (base) (base))
     (Tm.imp (Tm.all (base) (Tm.imp (Tm.app (Tm.app (Tm.named "inb") (Tm.db 0)) (Tm.db 3))
                                     (eq:of (E) (Tm.app (Tm.db 2) (Tm.db 0)) (Tm.app (Tm.db 1) (Tm.db 0)))))
             (eq:of (E) (bind (Tm.db 2) (Tm.db 1)) (bind (Tm.db 2) (Tm.db 0))))))))
!(conv:declare-binder (Tm.named "bind") "bind_family")
!(let* (($t (bind (zero) (Tm.lam (base) (add (Tm.db 0) (zero)))))
        ($expected (bind (zero) (Tm.lam (base) (Tm.db 0)))))
   (case (conv:run (E) (C0) (Cv.under (Cv.rule "add_zero" Prover.none)) $t)
     (((Conv $t2 $theorem) (UNDER binder (== $t2 $expected) (states $theorem $t $expected)))
      ($other (UNDER binder $other)))))
; the kernel does not make that step by itself
!(UNDER not-by-conversion
   (mg:try-convertible (T) (C0) (bind (zero) (Tm.lam (base) (add (Tm.db 0) (zero)))) (bind (zero) (Tm.lam (base) (Tm.db 0)))))
; a body the conversion leaves, and a form with no declared lemma, are left
!(UNDER body-unchanged (conv:run (E) (C0) (Cv.under (Cv.rule "add_zero" Prover.none)) (bind (zero) (Tm.lam (base) (suc (Tm.db 0))))))
!(UNDER no-lemma (conv:run (E) (C0) (Cv.under (Cv.rule "add_zero" Prover.none)) (pair (zero) (add (zero) (zero)))))
; the value of a term, and then inside its binder forms
!(let* (($t (pair (bind (zero) (Tm.lam (base) (add (Tm.db 0) (zero)))) (add (zero) (zero))))
        ($expected (pair (bind (zero) (Tm.lam (base) (Tm.db 0))) (zero))))
   (case (conv:run (E) (C0) (Cv.strong (arith)) $t)
     (((Conv $t2 $theorem) (UNDER strong (== $t2 $expected) (states $theorem $t $expected)))
      ($other (UNDER strong $other)))))
; the answer is kept like any other: asked again it is found; a term that
; was never asked about has none
!(UNDER strong-kept
   (case (conv:kept (E) (C0) (Cv.strong (arith)) (pair (bind (zero) (Tm.lam (base) (add (Tm.db 0) (zero)))) (add (zero) (zero))))
     ((() none) ($rows found)))
   (case (conv:kept (E) (C0) (Cv.strong (arith)) (pair (bind (zero) (Tm.lam (base) (suc (Tm.db 0)))) (zero)))
     ((() none) ($rows found))))
; a rule with one variable twice on its left side is found for its head
!(UNDER nonlinear (conv:nonlinear (E) (C0) choice (Tm.named "twin")) (conv:nonlinear (E) (C0) choice (Tm.named "pick")))

; 18. a rule that matches only after a view, its premise open.  pair (suc a)
; b = pair a b for small b; at pair bit two the left side matches once bit
; is seen as suc zero, and "two is small" is not proved.  The term is left
; and the rule is recorded with its premise, as for a rule that matches as
; the term stands
!(mg:axiom (AUTH) "pair_first_small"
   (Tm.all (base) (Tm.all (base) (Tm.imp (small (Tm.db 0)) (eq:of (E) (pair (suc (Tm.db 1)) (Tm.db 0)) (pair (Tm.db 1) (Tm.db 0)))))))
!(conv:declare (E) (C0) viewed-guard "pair_first_small")
!(VIEW-OPEN left (conv:run (E) (C0) (Cv.viewed viewed-guard (shown) Prover.none None) (pair (Tm.named "bit") (two))))
!(let* (($t (pair (Tm.named "bit") (two))) ($premise (small (two))))
   (VIEW-OPEN reason (collapse (match &evidence (ConvUndecided $e $c $t $name $why) ($name (== $why (Open $premise)))))))
; with a prover for the premise the rule applies after the view
!(let $t (pair (Tm.named "bit") (two))
   (case (conv:run (E) (C0) (Cv.viewed viewed-guard (shown) (Prover.horn (lemmas "small_zero" "small_suc") 4) None) $t)
     (((Conv $t2 $theorem) (VIEW-OPEN proved (== $t2 (pair (zero) (two))) (states $theorem $t (pair (zero) (two)))))
      ($other (VIEW-OPEN proved $other)))))
; where no view leads to the left side nothing is recorded: the rule for two
; successors does not match pair bit one after bit is seen as suc zero
!(mg:axiom (AUTH) "pair_first_two_small"
   (Tm.all (base) (Tm.all (base) (Tm.imp (small (Tm.db 0)) (eq:of (E) (pair (suc (suc (Tm.db 1))) (Tm.db 0)) (pair (Tm.db 1) (Tm.db 0)))))))
!(conv:declare (E) (C0) viewed-guard2 "pair_first_two_small")
!(VIEW-OPEN no-match (conv:run (E) (C0) (Cv.viewed viewed-guard2 (shown) Prover.none None) (pair (Tm.named "bit") (one))))
!(let $t (pair (Tm.named "bit") (one))
   (VIEW-OPEN no-match-recorded (collapse (match &evidence (ConvUndecided $e $c $t $name $why) $name))))

; 19. what is kept in a context with a variable and a hypothesis.  The
; theorem is closed over them and the theory keeps it under a name, so the
; row that holds it is plain; taken back in that context it states what it
; stated there
!(let* (($c1 (mg:term-binder (T) (C0) (base)))
        ($c2 (mg:proof-binder (T) $c1 (small (Tm.db 0))))
        ($theorem (mg:hyp (T) $c2 0))
        ($kept (conv:seal-out (E) $c2 $theorem))
        ($back (conv:seal-in (E) $c2 $kept)))
   (KEPT in-context
     (case $kept (((Named $name) named) ($other $other)))
     (== (mg:proposition $back) (mg:proposition $theorem))
     (== (mg:context $back) $c2)
     (case $kept
       (((Named $name) (== (mg:proposition (mg:known (T) $name)) (Tm.all (base) (Tm.imp (small (Tm.db 0)) (small (Tm.db 0))))))
        ($other $other)))))
; in a context with another hypothesis the kept theorem is not opened: the
; kernel refuses the hypothesis it is given
!(let* (($c1 (mg:term-binder (T) (C0) (base)))
        ($c2 (mg:proof-binder (T) $c1 (small (Tm.db 0))))
        ($other-context (mg:proof-binder (T) $c1 (small (suc (Tm.db 0)))))
        ($kept (conv:seal-out (E) $c2 (mg:hyp (T) $c2 0))))
   (KEPT other-context (case (conv:seal-in (E) $other-context $kept) (((Error $call $why) refused) ($opened opened)))))

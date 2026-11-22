; The live reading (P-DEF-4) on every tier: a compiled body, a compiled
; relation entered later and the search machine decide an executable
; occurrence when it is reached, against the current program.  A tier keeps a
; decision while the program it was made under is current; once the program
; has changed, the occurrence is decided again.

; A body that adds its own program's equations through a variable target, and
; the same body adding to a data space: only the program's equations count.
(= (adder $sp $h) (let $_ (add-atom $sp (= ($h $x) v)) ($h 1)))
!(bind! &data (new-space))
!(ToSelf (adder &self t60))
!(ToData (adder &data t61))
!(After (t60 1) (t61 1))

; The same through a variable target, with a data occurrence the body's plan
; decided before the addition.
(= (adder2 $sp) (let $_ (add-atom $sp (= (t63 $x) v)) (t63 1)))
!(VariableTarget (adder2 &self))
!(VariableTargetAfter (t63 1))
(= (adder3 $sp) (let $_ (add-atom $sp (= (t64 $x) v)) (Box (t64 1))))
!(bind! &d3 (new-space))
!(VariableDataSpace (adder3 &d3))

; A body that adds rows to a data space through a variable keeps its compiled
; occurrences: the program did not change.
(= (keep $sp $n) (let $_ (add-atom $sp (Fact $n)) (Pair (t62 $n) $n)))
!(Kept (keep &data 1))
!(Kept (keep &data 2))
!(Facts (collapse (match &data (Fact $x) $x)))

; Data built after a call that changes the program: the fields that follow
; the call are reached after it.
(= (adder71) (add-atom &self (= (t71 $x) v)))
!(Fields (Pair (adder71) (t71 1)))
(= (fields74) (Pair (add-atom &self (= (t74 $x) v)) (t74 1)))
!(FieldsInEquation (fields74))
(= (adder75) (add-atom &self (= (t75 $x) v)))
(= (fields75) (Pair (adder75) (t75 1) (Leaf 2)))
!(FieldsAfterCall (fields75))

; Arguments reached after the change, as a value and inside data.
(= (g77 $a) (Got $a))
(= (argval77) (let $u (add-atom &self (= (t77 $x) v)) (g77 (t77 1))))
!(Argument (argval77))
!(ArgumentInQuery (let $u (add-atom &self (= (t78 $x) v)) (g77 (t78 1))))
(= (adder79) (add-atom &self (= (t79 $x) v)))
(= (g2 $a $b) (Got2 $a $b))
(= (argmix79) (g2 (adder79) (t79 1)))
!(ArgumentAfterCall (argmix79))
(= (argdeep80) (let $u (add-atom &self (= (t80 $x) v)) (g77 (Box (t80 1)))))
!(ArgumentInData (argdeep80))

; An operation the compiled body applies to a data field's value.
(= (operation85) (let $u (add-atom &self (= (t85 $x) 5)) (+ (t85 1) 1)))
!(OperationOnData (operation85))
(= (member86) (let $u (add-atom &self (= (t86 $x) (a b))) (member a (t86 1))))
!(MembershipInData (collapse (member86)))

; A destination the caller demands meets the occurrence as it is reached:
; an output fixed in advance covers only occurrences no effect can precede.
(= (whole73) (let $u (add-atom &self (= (t73 $x) v)) (t73 1)))
!(Demanded (let v (whole73) yes))
(= (adder98) (add-atom &self (= (t98 $x) 3)))
(= (f98) (Pair (adder98) (t98 2)))
!(DemandedAfterCall (collapse (let (Pair $a 3) (f98) yes)))
; A goal proved to leave the program unchanged keeps the output fixed in
; advance, so an equation that cannot meet the destination is not entered.
(= (loop97) (loop97))
(= (f97) (Pair (loop97) (Leaf 2)))
!(Pruned (collapse (let (Pair $a (Leaf 3)) (f97) yes)))

; Plans selected before a change and entered after it: a later alternative,
; a relation called after the change, a lambda made before it.
(= (alt89) (let $u (add-atom &self (= (t89 $x) v)) first))
(= (alt89) (t89 1))
!(Alternatives (collapse (alt89)))
(= (alt90) (let $u (add-atom &self (= (t90 $x) v)) first))
(= (alt90) (Wrap (t90 1)))
!(AlternativesInData (collapse (alt90)))
(= (mk91) (t91 1))
(= (top91) (let $u (add-atom &self (= (t91 $x) v)) (mk91)))
!(CalleeAfterChange (top91))
(= (lam92) (let $f (|-> ($x) (t92 $x)) (let $u (add-atom &self (= (t92 $y) v)) ($f 1))))
!(LambdaAfterChange (lam92))
(= (pure99 $x) (t99 $x))
!(PureBefore (pure99 1))
(= (install99) (let $u (add-atom &self (= (t99 $x) z)) (pure99 1)))
!(PureAfter (install99))

; A let that puts its destructured head back into a data result: the
; result's shape is met early only where no goal before it may change the
; program, and a constructor that has become a call answers as one.
(= (tagged2 $xs) (let ($h $t) (decons-atom $xs) (Tagged2 (cons-atom $h $t) $t)))
!(LateBefore (tagged2 (a b)))
!(LateDemanded (let (Tagged2 (a $r) $s) (tagged2 (a b)) ($r $s)))
(= (install2) (add-atom &self (= (Tagged2 $l $r) (Joined $l $r))))
!(LateInstalled (install2))
!(LateAfter (tagged2 (a b)))
(= (tagged3 $xs)
   (let ($h $t) (decons-atom $xs)
      (let $u (add-atom &self (= (Tagged3 $l $r) (Joined3 $l $r)))
         (Tagged3 (cons-atom $h $t) $t))))
!(LateChangedInBody (tagged3 (a b)))

; Removal in both directions: the last equation removed in a body, directly
; and through a variable, and in an alternative selected before it.
(= (t95 $x) v)
(= (rmv95) (let $u (remove-atom &self (= (t95 $x) v)) (t95 1)))
!(RemovedInBody (rmv95))
(= (t96 $x) w)
(= (rmvar96 $sp) (let $u (remove-atom $sp (= (t96 $x) w)) (t96 1)))
!(RemovedThroughVariable (rmvar96 &self))
(= (rm93 $x) present)
(= (alt93) (let $u (remove-atom &self (= (rm93 $x) present)) gone))
(= (alt93) (rm93 1))
!(RemovedBeforeAlternative (collapse (alt93)))
(= (readd96) (let $u (add-atom &self (= (t96 $x) back)) (t96 2)))
!(Readded (readd96))

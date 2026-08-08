; Prime equation-call sharing tournament.  These forms characterize a policy
; through values and per-answer receipts; their current output is evidence,
; not a declaration that the baseline scheduler is normative.

; 1. An unused faulting argument is never observed.
(= (tournament:c01 $unused) c01-unused)
!(tournament:c01 (== 1 "unused-fault"))

; 2. Two strict candidates subscribe to the same source argument.
(= (tournament:c02 A) c02-a)
(= (tournament:c02 B) c02-b)
!(tournament:c02 (superpose (A B)))

; 3. The wildcard uses the argument in its RHS.
(= (tournament:c03 A) (c03-strict A))
(= (tournament:c03 $x) (c03-wild $x))
!(tournament:c03 (superpose (A B)))

; 4. The wildcard RHS is constant and has empty support.
(= (tournament:c04 A) c04-strict)
(= (tournament:c04 $unused) c04-constant)
!(tournament:c04 (superpose (A B)))

; 5. The candidates demand disjoint arguments.
(= (tournament:c05 A $y) c05-left)
(= (tournament:c05 $x B) c05-right)
!(tournament:c05 (superpose (A X)) (superpose (B Y)))

; 6. The same disjoint supports in reversed declaration order.
(= (tournament:c06 $x B) c06-right)
(= (tournament:c06 A $y) c06-left)
!(tournament:c06 (superpose (A X)) (superpose (B Y)))

; 7. Overlapping supports {arg1}, {arg2}, and {arg1,arg2}.
(= (tournament:c07 A $y) c07-left)
(= (tournament:c07 $x B) c07-right)
(= (tournament:c07 A B) c07-both)
!(tournament:c07 (superpose (A X)) (superpose (B Y)))

; 8. One aliased explicit cell differs from two textual allocations.
(= (tournament:c08 A A) c08-aa)
(= (tournament:c08 A B) c08-ab)
(= (tournament:c08 B A) c08-ba)
(= (tournament:c08 B B) c08-bb)
!(let $shared (delay (superpose (A B)))
   (tournament:c08 (force $shared) (force $shared)))
!(tournament:c08
   (force (delay (superpose (A B))))
   (force (delay (superpose (A B)))))

; 9. One context shares its binding; independently built contexts do not.
(= (tournament:c09 A A) c09-aa)
(= (tournament:c09 A B) c09-ab)
(= (tournament:c09 B A) c09-ba)
(= (tournament:c09 B B) c09-bb)
!(let $shared (ctx:bind ctx:empty choice (superpose (A B)))
   (tournament:c09
     (ctx:get $shared choice)
     (ctx:get $shared choice)))
!(let $left (ctx:bind ctx:empty choice (superpose (A B)))
   (let $right (ctx:bind ctx:empty choice (superpose (A B)))
     (tournament:c09
       (ctx:get $left choice)
       (ctx:get $right choice))))

; 10. Structurally duplicate admitted equations remain two occurrences.
(= (tournament:c10 A) c10-duplicate)
(= (tournament:c10 A) c10-duplicate)
!(tournament:c10 A)

; 11. delay shares one choice while resample intentionally samples afresh.
!(let $shared (delay (superpose (A B)))
   (c11-delay (force $shared) (force $shared)))
!(let $fresh (resample (superpose (A B)))
   (c11-resample (force $fresh) (force $fresh)))

; 12. Effects before a choice and after a choice stay in branch receipts.
!(bind! &tournament:c12-before-state (new-state 0))
(= (tournament:c12-before)
   (let $old (get-state &tournament:c12-before-state)
     (let $_ (change-state! &tournament:c12-before-state (+ $old 1))
       (superpose (A B)))))
(= (tournament:c12-before-use A) c12-before-a)
(= (tournament:c12-before-use B) c12-before-b)
!(tournament:c12-before-use (tournament:c12-before))
!(get-state &tournament:c12-before-state)

!(bind! &tournament:c12-after-state (new-state 0))
(= (tournament:c12-after)
   (let $choice (superpose (A B))
     (let $old (get-state &tournament:c12-after-state)
       (let $_ (change-state! &tournament:c12-after-state (+ $old 1))
         $choice))))
(= (tournament:c12-after-use A) c12-after-a)
(= (tournament:c12-after-use B) c12-after-b)
!(tournament:c12-after-use (tournament:c12-after))
!(get-state &tournament:c12-after-state)

; 13. Divergent sibling writes conflict; repeated equal observations agree.
!(bind! &tournament:c13-state (new-state 0))
(= (tournament:c13-conflict)
   (let $choice (superpose (1 2))
     (let $_ (change-state! &tournament:c13-state $choice)
       (c13-conflict $choice))))
!(tournament:c13-conflict)
!(get-state &tournament:c13-state)
(= (tournament:c13-compatible A) c13-compatible)
(= (tournament:c13-compatible A) c13-compatible)
!(tournament:c13-compatible (superpose (A B)))

; 14. A context round-trip through a space preserves one hidden cell.
!(bind! &tournament:c14-store (new-space))
!(let $context (ctx:bind ctx:empty choice (superpose (A B)))
   (add-atom &tournament:c14-store (StoredTournamentContext $context)))
!(match &tournament:c14-store (StoredTournamentContext $saved)
   (c14-roundtrip
     (ctx:get $saved choice)
     (ctx:get $saved choice)))

; 15. A known first argument must not cancel candidates whose second support
; is still suspended.
(= (tournament:c15 A $y) c15-left)
(= (tournament:c15 $x B) c15-right)
(= (tournament:c15 A B) c15-both)
!(tournament:c15 A (superpose (B Y)))

; 16. A repeated variable is a cross-path equality constraint.  Neither
; argument alone can refute it while the other argument remains suspended.
(= (tournament:c16 $x $x) (c16-equal $x))
!(tournament:c16 (superpose (A B)) (superpose (A B)))

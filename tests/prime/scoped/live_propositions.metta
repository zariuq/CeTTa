; Propositions are ordinary atoms.  A running program can build them, fill
; them with computed values, search for their proofs, publish what it found,
; and read the live theory back out of the space.  The judgment decides;
; the program only proposes.

; Every set is a member of its power set: the law of the power set at the set
; itself, whose second half, read from right to left, applies to the proof
; that every member of the set is a member of it.
!(set:theorem power-self (all set (lam x (In x (Power x))))
   (pf:all-intro
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) (pf:var 0)) (pf:var 0))
           (imp (all set (lam y (imp (In y (pf:var 0)) (In y (pf:var 0)))))
                (In (pf:var 0) (Power (pf:var 0)))))
         (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
       (pf:all-intro (pf:imp-intro (pf:hyp 0))))))

; 1. A program builds a proposition and hands it to a judgment.
(= (member-claim $x $y) (In $x $y))
!(let $claim (member-claim Empty (Power Empty)) (set:check $claim prop))
!(let $claim (member-claim Empty Empty)
   (set:proves (pf:all-elim (pf:known power-self) Empty) $claim))

; 2. A proposition instantiated by program values: every branch is checked.
(= (pick) (superpose (Empty (Power Empty) (UnivOf Empty))))
!(let $w (pick)
   (set:proves (pf:all-elim (pf:known power-self) $w) (In $w (Power $w))))

; 3. Proof search as a program: candidates are proposed, the judgment
;    disposes, and only accepted candidates survive.
(= (universal-names) (superpose (emptyLaw power-self universeIn)))
(= (find-proof $goal)
   (let* (($name (universal-names))
          ($w (pick))
          ($candidate (pf:all-elim (pf:known $name) $w)))
     (case (set:proves $candidate $goal) ((True $candidate)))))
!(find-proof (In Empty (Power Empty)))
!(find-proof (In (Power Empty) (UnivOf (Power Empty))))
!(collapse (find-proof (In Empty Empty)))

; 4. The search result is published with its proof and dependencies, and
;    programs read the live theory back out of the space.
!(let $p (find-proof (In Empty (Power Empty)))
   (set:theorem found-membership (In Empty (Power Empty)) $p))
!(set:known-proof found-membership)
!(match &self (set:known $n $prop theorem $proof $deps $rev $sig) (Theorem $n $prop))
!(set:proves (pf:known found-membership) (In Empty (Power Empty)))

; 5. One vocabulary, two faces.  An experimental executable union computes
;    on finite representations and may be wrong; it has no mathematical
;    authority, and proofs about the signature's Union do not see it.
(= (set:union (fin $xs) (fin $ys)) (fin ($xs $ys)))
!(set:union (fin (1 2)) (fin (3)))
!(set:union (fin ()) (fin (1)))
!(set:check (set:union Empty Empty) set)
!(set:proves (pf:all-elim (pf:known power-self) Empty) (In Empty (Power Empty)))

; 6. The theory changes while the program runs; reuse follows the change.
;    A consequence of a live assumption stops being reusable when the
;    assumption is withdrawn, while the theorem that never used it stays.
!(set:theorem membership-implication (imp (In Empty Empty) (In Empty (Power Empty)))
              (pf:imp-intro (pf:known found-membership)))
!(set:axiom live-assumption (In Empty Empty))
!(set:theorem live-consequence (In Empty (Power Empty))
              (pf:imp-elim (pf:known membership-implication) (pf:known live-assumption)))
!(set:proves (pf:known live-consequence) (In Empty (Power Empty)))
!(match &self (set:known live-assumption $p $o $pf $d $r $sig)
        (remove-atom &self (set:known live-assumption $p $o $pf $d $r $sig)))
!(set:proves (pf:known live-consequence) (In Empty (Power Empty)))
!(set:proves (pf:known found-membership) (In Empty (Power Empty)))

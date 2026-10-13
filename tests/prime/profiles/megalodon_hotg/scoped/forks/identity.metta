(hol:profile megalodon-hotg)

; FORK-ID: are two proofs of one proposition the same thing?
; Application sees only the proposition; learning sees the route; revision
; sees which route survives when a premise is withdrawn.

; Every set is a member of its power set: the law of the power set at the set
; itself, whose second half, read from right to left, applies to the proof
; that every member of the set is a member of it.
!(hol:theorem power-self (all set (lam x (In x (Power x))))
   (pf:all-intro
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) (pf:var 0)) (pf:var 0))
           (imp (all set (lam y (imp (In y (pf:var 0)) (In y (pf:var 0)))))
                (In (pf:var 0) (Power (pf:var 0)))))
         (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
       (pf:all-intro (pf:imp-intro (pf:hyp 0))))))
!(hol:theorem membership-implication (imp (In Empty Empty) (In Empty (Power Empty)))
              (pf:imp-intro (pf:all-elim (pf:known power-self) Empty)))
!(hol:axiom live-assumption (In Empty Empty))
!(hol:theorem via-power (In Empty (Power Empty))
              (pf:all-elim (pf:known power-self) Empty))
!(hol:theorem via-implication (In Empty (Power Empty))
              (pf:imp-elim (pf:known membership-implication) (pf:known live-assumption)))

; Application view: the same proposition.
!(hol:known-proposition via-power)
!(hol:known-proposition via-implication)
; Learning view: two routes.
!(hol:known-proof via-power)
!(hol:known-proof via-implication)
; Dependency view: different support.
!(match &self (hol:known via-power $p $o $pf $d $r $sig) $d)
!(match &self (hol:known via-implication $p $o $pf $d $r $sig) $d)
; Revision: withdraw the assumption; one route survives, the other does not.
!(match &self (hol:known live-assumption $p $o $pf $d $r $sig)
        (remove-atom &self (hol:known live-assumption $p $o $pf $d $r $sig)))
!(hol:proves (pf:known via-power) (In Empty (Power Empty)))
!(hol:proves (pf:known via-implication) (In Empty (Power Empty)))

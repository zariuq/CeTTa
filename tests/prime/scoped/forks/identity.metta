; FORK-ID: are two proofs of one proposition the same thing?
; Application sees only the proposition; learning sees the route; revision
; sees which route survives when a premise is withdrawn.

!(set:theorem membership-implication (imp (In Empty Empty) (In Empty (Power Empty)))
              (pf:imp-intro (pf:all-elim (pf:known PowerI) Empty)))
!(set:axiom live-assumption (In Empty Empty))
!(set:theorem via-power (In Empty (Power Empty))
              (pf:all-elim (pf:known PowerI) Empty))
!(set:theorem via-implication (In Empty (Power Empty))
              (pf:imp-elim (pf:known membership-implication) (pf:known live-assumption)))

; Application view: the same proposition.
!(set:known-proposition via-power)
!(set:known-proposition via-implication)
; Learning view: two routes.
!(set:known-proof via-power)
!(set:known-proof via-implication)
; Dependency view: different support.
!(match &self (set:known via-power $p $o $pf $d $r $sig) $d)
!(match &self (set:known via-implication $p $o $pf $d $r $sig) $d)
; Revision: withdraw the assumption; one route survives, the other does not.
!(match &self (set:known live-assumption $p $o $pf $d $r $sig)
        (remove-atom &self (set:known live-assumption $p $o $pf $d $r $sig)))
!(set:proves (pf:known via-power) (In Empty (Power Empty)))
!(set:proves (pf:known via-implication) (In Empty (Power Empty)))

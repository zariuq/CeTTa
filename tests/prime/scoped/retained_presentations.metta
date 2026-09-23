; Three different promises over one proposition with two accepted proofs.
; Fidelity of a retained presentation: parsed syntax as atoms.  Proofs use a
; de Bruijn hypothesis language, so no alpha-renaming question arises for
; them; propositions are retained as written.  Nothing retains bytes.

!(set:theorem via-power (In Empty (Power Empty)) (pf:all-elim (pf:known PowerI) Empty))
!(set:theorem membership-implication (imp (In Empty Empty) (In Empty (Power Empty)))
              (pf:imp-intro (pf:all-elim (pf:known PowerI) Empty)))
!(set:axiom assumed (In Empty Empty))
!(set:theorem via-implication (In Empty (Power Empty))
              (pf:imp-elim (pf:known membership-implication) (pf:known assumed)))

; Ordinary observation: the same proposition.
!(set:known-proposition via-power)
!(set:known-proposition via-implication)
; Retained originals: two presentations.
!(set:known-proof via-power)
!(set:known-proof via-implication)
; Reconstruction: a search chooses a representative for the common
; observation; it is labelled as such and never called an original.
(= (universal-names) (superpose (EmptyE PowerI UnivOfI)))
(= (witnesses) (superpose (Empty (Power Empty))))
(= (reconstruct $goal)
   (let* (($n (universal-names)) ($w (witnesses))
          ($c (pf:all-elim (pf:known $n) $w)))
     (case (set:proves $c $goal) ((True (reconstruction $c))))))
!(reconstruct (In Empty (Power Empty)))
; A missing reference never falls back to reconstruction.
!(set:known-proof nothing)

; Lifetime.  Unrelated publication and an unrelated live revision keep
; every reuse valid.
!(set:axiom unrelated (In Empty (UnivOf Empty)))
!(set:proves (pf:known via-implication) (In Empty (Power Empty)))
!(add-atom &self (note something))
!(set:proves (pf:known via-implication) (In Empty (Power Empty)))
; A relevant assumption change: applicability is lost, inspection is not.
!(match &self (set:known assumed $p $o $pf $d $r $s)
        (remove-atom &self (set:known assumed $p $o $pf $d $r $s)))
!(try (set:proves (pf:known via-implication) (In Empty (Power Empty))))
!(set:known-proof via-implication)
!(set:proves (pf:known via-power) (In Empty (Power Empty)))
; Key reuse with the same proposition restores applicability; with another
; proposition it does not.
!(set:axiom assumed (In Empty Empty))
!(set:proves (pf:known via-implication) (In Empty (Power Empty)))
!(match &self (set:known assumed $p $o $pf $d $r $s)
        (remove-atom &self (set:known assumed $p $o $pf $d $r $s)))
!(set:axiom assumed (In Empty (UnivOf Empty)))
!(try (set:proves (pf:known via-implication) (In Empty (Power Empty))))
; Wrong store: a record copied into another space carries no authority there
; until it is checked there.
!(bind! &other (new-space))
!(match &self (set:known via-power $p $o $pf $d $r $s)
        (add-atom &other (set:known via-power $p $o $pf $d $r $s)))
!(try &other (set:proves (pf:known via-power) (In Empty (Power Empty))))
!(set:recheck &other via-power)
!(set:proves &other (pf:known via-power) (In Empty (Power Empty)))

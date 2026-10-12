(hol:profile megalodon-hotg)

; Three different promises over one proposition with two accepted proofs.
; Fidelity of a retained presentation: parsed syntax as atoms.  Proofs use a
; de Bruijn hypothesis language, so no alpha-renaming question arises for
; them; propositions are retained as written.  Nothing retains bytes.

; Empty is a member of its power set, by the law of the power set at Empty.
!(hol:theorem via-power (In Empty (Power Empty))
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) Empty) Empty)
         (imp (all set (lam y (imp (In y Empty) (In y Empty)))) (In Empty (Power Empty))))
       (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
     (pf:all-intro (pf:imp-intro (pf:hyp 0)))))
!(hol:theorem membership-implication (imp (In Empty Empty) (In Empty (Power Empty)))
   (pf:imp-intro
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) Empty) Empty)
         (imp (all set (lam y (imp (In y Empty) (In y Empty)))) (In Empty (Power Empty))))
       (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
     (pf:all-intro (pf:imp-intro (pf:hyp 0))))))
!(hol:axiom assumed (In Empty Empty))
!(hol:theorem via-implication (In Empty (Power Empty))
              (pf:imp-elim (pf:known membership-implication) (pf:known assumed)))

; Ordinary observation: the same proposition.
!(hol:known-proposition via-power)
!(hol:known-proposition via-implication)
; Retained originals: two presentations.
!(hol:known-proof via-power)
!(hol:known-proof via-implication)
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
; Reconstruction: a search chooses a representative for the common
; observation; it is labelled as such and never called an original.
(= (universal-names) (superpose (emptyLaw power-self universeIn)))
(= (witnesses) (superpose (Empty (Power Empty))))
(= (reconstruct $goal)
   (let* (($n (universal-names)) ($w (witnesses))
          ($c (pf:all-elim (pf:known $n) $w)))
     (case (hol:proves $c $goal) ((True (reconstruction $c))))))
!(reconstruct (In Empty (Power Empty)))
; A missing reference never falls back to reconstruction.
!(hol:known-proof nothing)

; Lifetime.  Unrelated publication and an unrelated live revision keep
; every reuse valid.
!(hol:theorem unrelated (In Empty (UnivOf Empty)) (pf:all-elim (pf:known universeIn) Empty))
!(hol:proves (pf:known via-implication) (In Empty (Power Empty)))
!(add-atom &self (note something))
!(hol:proves (pf:known via-implication) (In Empty (Power Empty)))
; A relevant assumption change: applicability is lost, inspection is not.
!(match &self (hol:known assumed $p $o $pf $d $r $s)
        (remove-atom &self (hol:known assumed $p $o $pf $d $r $s)))
!(try (hol:proves (pf:known via-implication) (In Empty (Power Empty))))
!(hol:known-proof via-implication)
!(hol:proves (pf:known via-power) (In Empty (Power Empty)))
; Key reuse with the same proposition restores applicability; with another
; proposition it does not.
!(hol:axiom assumed (In Empty Empty))
!(hol:proves (pf:known via-implication) (In Empty (Power Empty)))
!(match &self (hol:known assumed $p $o $pf $d $r $s)
        (remove-atom &self (hol:known assumed $p $o $pf $d $r $s)))
!(hol:axiom assumed (In Empty (UnivOf Empty)))
!(try (hol:proves (pf:known via-implication) (In Empty (Power Empty))))
; Wrong store: a record copied into another space carries no authority there
; until it is checked there.
!(bind! &other (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile megalodon-hotg)) $profile-space)))
!(match &self (hol:known via-power $p $o $pf $d $r $s)
        (add-atom &other (hol:known via-power $p $o $pf $d $r $s)))
!(try &other (hol:proves (pf:known via-power) (In Empty (Power Empty))))
!(hol:recheck &other via-power)
!(hol:proves &other (pf:known via-power) (In Empty (Power Empty)))

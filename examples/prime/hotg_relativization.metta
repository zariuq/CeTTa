; Moving theorems between the HOTG set theory and the logic alone, both ways.
; A HOTG theorem is relativized: its constants become bound variables and its
; eleven laws become hypotheses, and the logic alone checks the result. The
; relativization is included back into a HOTG space, checked there afresh,
; and applied to the HOTG constants and laws, which proves the original
; statement again. The way from the logic into HOTG adds commitments: HOTG
; proves that there are two distinct sets, a statement of the logic alone
; that the logic alone does not prove.
(scope:profile lexical-fresh per-call reference)
!(import! &self ../../lib/prime/hol_relativization.metta)
!(bind! &sets (let $s (new-space)
  (let $p (add-atom $s (set:profile megalodon-hotg))
    (let $r (add-atom $s (set:rule hol-equality)) $s))))
!(bind! &logic (let $s (new-space)
  (let $p (add-atom $s (hol:profile base))
    (let $r (add-atom $s (hol:rule hol-equality)) $s))))

; HOTG theorems.
!(set:theorem &sets empty-in-universe (In Empty (UnivOf Empty))
   (pf:all-elim (pf:known universeIn) Empty))
!(set:theorem &sets empty-in-power-empty (In Empty (Power Empty))
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) Empty) Empty)
         (imp (all set (lam y (imp (In y Empty) (In y Empty)))) (In Empty (Power Empty))))
       (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
     (pf:all-intro (pf:imp-intro (pf:hyp 0)))))
!(set:theorem &sets empty-not-power-empty (imp (eq set Empty (Power Empty)) Falsum)
   (pf:imp-intro
     (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty)
       (pf:imp-elim
         (pf:eq-prop-elim (pf:eq-app-arg set prop (In Empty) (pf:eq-symm (pf:hyp 0))))
         (pf:known empty-in-power-empty)))))
; There are two distinct sets: every r that follows from two distinct sets
; holds.
(= (example:two-distinct-sets)
   (function (return
     (all prop (lam r (imp (all set (lam x (all set (lam y (imp (imp (eq set x y) Falsum) r)))))
                           r))))))
!(let $statement (example:two-distinct-sets)
   (let $q (cons-atom quote ($statement))
     (set:theorem &sets two-distinct-sets $q
       (pf:all-intro (pf:imp-intro
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:hyp 0) Empty) (Power Empty))
           (pf:known empty-not-power-empty)))))))

; Into the logic alone: relativize. The relativization of a theorem cites the
; relativizations of the theorems its proof cites.
!(Relativized (hol-rel:relativize &sets &logic empty-in-universe))
!(Relativized (hol-rel:relativize &sets &logic two-distinct-sets))
!(collapse (match &logic (hol-rel:relativized $source $result) ($source $result)))
!(let $p (hol:known-proposition &logic rel:empty-in-universe) (StatementHead (car-atom $p) (index-atom $p 1)))

; Back into HOTG: include, then instantiate with the HOTG constants and laws.
!(Included (hol-rel:include &logic &sets rel:two-distinct-sets))
!(let $statement (example:two-distinct-sets)
   (Instantiated (hol-rel:instantiate &sets inc:rel:two-distinct-sets two-distinct-sets-again $statement)))
!(let $a (set:known-proposition &sets two-distinct-sets)
   (let $b (set:known-proposition &sets two-distinct-sets-again)
     (SameStatement (== $a $b))))

; The logic alone does not accept the HOTG proof of the bare statement: its
; witnesses are HOTG constants, which the logic alone does not have.
!(let $statement (example:two-distinct-sets)
   (let $q (cons-atom quote ($statement))
     (let $verdict (try (hol:proves &logic
                    (pf:all-intro (pf:imp-intro
                      (pf:imp-elim (pf:all-elim (pf:all-elim (pf:hyp 0) Empty) (Power Empty))
                        (pf:known empty-not-power-empty))))
                    $q))
       (BareLogic $verdict))))

; A HOTG theorem is not cited in the logic alone without the bridge.
!(let $verdict (try (hol:proves &logic (pf:known two-distinct-sets) (all prop (lam p (imp p p)))))
   (UnbridgedCitation $verdict))

; A proof form the relativization does not read is refused, by name.
!(set:theorem &sets refl-by-verb (all set (lam x (imp (In x Empty) (In x Empty))))
   (pf:fix x (pf:assume h h)))
!(Unsupported (hol-rel:relativize &sets &logic refl-by-verb))

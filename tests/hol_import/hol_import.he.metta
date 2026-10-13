; Prime's native higher-order logic and its HOTG set theory in the HE
; extended profile.  Theory spaces are explicit, and every answer is data.
!(import! &self hol)
!(import! &self set)
!(bind! &logic (new-space))
!(add-atom &logic (hol:profile base))
!(hol:theorem &logic self-imp (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(hol:proves &logic (pf:known self-imp) (all prop (lam p (imp p p))))
!(hol:known-proposition &logic self-imp)
; A wrong proof is refuted.
!(hol:proves &logic (pf:hyp 0) (all prop (lam p (imp p p))))
; An undecided judgment stays as written; try reports why.
!(hol:try (hol:proves &logic (pf:known missing) (all prop (lam p (imp p p)))))
; The HOTG set theory, a set: theory over the logic.
!(bind! &hotg (new-space))
!(add-atom &hotg (set:profile megalodon-hotg))
!(set:signature &hotg base-logic)
!(set:of &hotg Empty)
!(set:proves &hotg (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty)))
; The logic alone has none of HOTG's laws.
!(hol:try (hol:known-proposition &logic universeIn))
; A theorem of one theory space is not cited in another.
!(set:try (set:proves &hotg (pf:known self-imp) (all prop (lam p (imp p p)))))
; A judgment of the other namespace is not read: the logic's space is not a
; set space, nor the HOTG space a space of the logic alone.
!(let $r (set:proves &logic (pf:all-intro (pf:imp-intro (pf:hyp 0))) (all prop (lam p (imp p p)))) (car-atom $r))
!(let $r (hol:of &hotg Empty) (car-atom $r))

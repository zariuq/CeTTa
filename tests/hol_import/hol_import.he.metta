; Prime's native higher-order logic in the HE extended profile.  Theory
; spaces are explicit, and every answer is data.
!(import! &self hol)
!(bind! &logic (new-space))
!(add-atom &logic (hol:profile core))
!(hol:theorem &logic self-imp (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(hol:proves &logic (pf:known self-imp) (all prop (lam p (imp p p))))
!(hol:known-proposition &logic self-imp)
; A wrong proof is refuted.
!(hol:proves &logic (pf:hyp 0) (all prop (lam p (imp p p))))
; An undecided judgment stays as written; try reports why.
!(hol:try (hol:proves &logic (pf:known missing) (all prop (lam p (imp p p)))))
; The HOTG set theory over the logic.
!(bind! &sets (new-space))
!(add-atom &sets (hol:profile megalodon-hotg))
!(hol:of &sets Empty)
!(hol:proves &sets (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty)))
; The logic alone has none of HOTG's laws.
!(hol:try (hol:known-proposition &logic universeIn))
; A theorem of one theory space is not cited in another.
!(hol:try (hol:proves &sets (pf:known self-imp) (all prop (lam p (imp p p)))))
; A space without a profile of the logic is not a theory space: the
; judgment stays as written.
!(bind! &plain (new-space))
!(let $r (hol:proves &plain (pf:all-intro (pf:imp-intro (pf:hyp 0))) (all prop (lam p (imp p p)))) (car-atom $r))

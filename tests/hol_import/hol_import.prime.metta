; Importing hol makes the importing space a theory space of the logic alone.
(scope:profile lexical-fresh per-call reference)
!(import! &self hol)
!(hol:theorem self-imp (all prop (lam p (imp p p))) (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(hol:proves (pf:known self-imp) (all prop (lam p (imp p p))))
; The logic alone has none of HOTG's laws.
!(try (hol:known-proposition universeIn))
; A set judgment is not read in a theory space of the logic.
!(set:proves (pf:known self-imp) (all prop (lam p (imp p p))))

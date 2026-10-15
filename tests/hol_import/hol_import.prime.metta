; Importing hol makes the importing space a theory space of the logic alone;
; a set theory over it lives in its own space.
(scope:profile lexical-fresh per-call reference)
!(import! &self hol)
!(import! &self set)
!(hol:theorem self-imp (all prop (lam p (imp p p))) (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(hol:proves (pf:known self-imp) (all prop (lam p (imp p p))))
!(hol:signature base-logic)
; The logic alone has none of HOTG's laws.
!(try (hol:known-proposition universeIn))
; A set judgment is not read in a theory space of the logic.
!(set:proves (pf:known self-imp) (all prop (lam p (imp p p))))
; The HOTG set theory over the same logic.
!(bind! &hotg (let $s (new-space) (let $_ (add-atom $s (set:profile megalodon-hotg)) $s)))
!(set:signature &hotg base-logic)
!(set:proves &hotg (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty)))

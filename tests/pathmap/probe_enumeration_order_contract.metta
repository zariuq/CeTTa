; PROBE (2026-08-05, Fable): the enumeration-order contract, empirically
; pinned.  The pathmap engine enumerates PATH-SORTED with multiplicity
; retained; the native engine enumerates in INSERTION order.  Both are
; lawful under bag-level spec equivalence; each engine's order is
; deterministic and is re-established by every shadow refresh.  Any delta
; shadow-sync protocol must reproduce the path-sorted order exactly
; (rank/ordered-insert), or it diverges from what a refresh would produce.
;   pathmap: [(r 1 b), (r 1 b), (r 5 c), (r 9 a)]
;            [(r 1 b), (r 1 b), (r 3 d), (r 5 c), (r 9 a)]
;   native : [(r 9 a), (r 1 b), (r 5 c), (r 1 b)]
;            [(r 9 a), (r 1 b), (r 5 c), (r 1 b), (r 3 d)]
!(bind! &f (new-space))
!(add-atom &f (edge 9 a))
!(add-atom &f (edge 1 b))
!(add-atom &f (edge 5 c))
!(add-atom &f (edge 1 b))
!(match &f (edge $x $y) (r $x $y))
!(add-atom &f (edge 3 d))
!(match &f (edge $x $y) (r $x $y))

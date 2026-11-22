; A let-bound cut commits the preceding overlapping deterministic clause.
; This is distinct from a conditional cut: the cut is reached whenever the
; first clause is selected, so the later overlapping clause is unreachable.

(: let-cut-overlap (-[det]-> Atom Atom))

(= (let-cut-overlap $value)
   (let $commit (cut) $value))
(= (let-cut-overlap a) a)

!(test (collapse (let-cut-overlap a)) (a))

; HE success-priority rule: if an expression has any non-error results, sibling
; error results are filtered away before they escape evaluation.

(= (success-priority-probe) ())
(= (success-priority-probe) (car-atom ()))

!(collapse (success-priority-probe))

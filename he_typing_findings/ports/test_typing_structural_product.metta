; Ported from PeTTa examples/types.metta:15. An arbitrary expression whose head
; is not a function is typed element-wise as a product. Native HE and the
; regrounded type-of AGREE here (regrounded returns (type-set (A B))).
(: a A) (: b B)
!(assertEqual (get-type (a b)) (A B))

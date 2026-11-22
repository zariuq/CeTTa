; The symmetric self-membership case likewise succeeds without constructing
; a cyclic type substitution.

(: narrow-parametric (-> (| $a Number) $a))
(= (narrow-parametric $value) $value)

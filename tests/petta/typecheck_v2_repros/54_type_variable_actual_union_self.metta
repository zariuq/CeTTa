; A type variable already occurring in a required union is a member of that
; union.  The checker must not encode the fact as the cyclic substitution
; $a := (| $a Number).

(: widen-parametric (-> $a (| $a Number)))
(= (widen-parametric $value) $value)

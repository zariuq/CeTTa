(: spine-box (-> $a (Box $a)))
(: spine-pair (-> $a $b (Pair $a $b)))
(= (spine-body $unused) (let $t (get-type (spine-box $t)) $t))

; A shared output sees the row while its tail is still open.
!(let $t (get-type (spine-box $t)) $t)
!(spine-body unused)
!(let $t (get-type (spine-pair $t 1)) $t)
; An independent output retains the fresh type variable.
!(get-type (spine-box $x))
!(let (Box Number) (get-type (spine-box $x)) (spine-bound $x))

; An authored classifier forces the relational type-query path.
(= (get-type (spine-authored)) Authored)
!(let $t (get-type (spine-box $t)) $t)
!(spine-body unused)
!(let $t (get-type (spine-pair $t 1)) $t)
!(get-type (spine-box $x))
!(let (Box Number) (get-type (spine-box $x)) (spine-bound $x))
!(get-type (spine-authored))

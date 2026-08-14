; Constructor patterns type their branch-local fields from the scrutinee.
(: Box Type)
(: boxed (-[det]-> Number Box))
(: missing Box)

(: case-number (-[det]-> Box Number))
(= (case-number $box)
   (case $box
      (((boxed $value) $value)
       (missing 0))))

(: reject-case-string (-[det]-> Box String))
(= (reject-case-string $box)
   (case $box
      (((boxed $value) $value)
       (missing "missing"))))

; A uniquely matching union member supplies the branch-local list fields.
(: union-list-tail
   (-[det]-> (| Number (List Number)) (List Number)))
(= (union-list-tail $value)
   (case $value
      ((() ())
       ((cons $_head $tail) $tail)
       ($_number ()))))

; The same narrowing must expose an incompatible list element instead of
; laundering it through gradual ignorance.
(: reject-union-list-head-string
   (-[det]-> (| Number (List Number)) String))
(= (reject-union-list-head-string $value)
   (case $value
      ((() "empty")
       ((cons $head $_tail) (the String $head))
       ($_number "number"))))

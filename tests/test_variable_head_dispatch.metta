; HE-backed regression: a fresh variable in function position remains data.
; Unrelated unary equations in scope must not capture ($newvar foo).
(= (make_var) $x)
(= (fromNum $n) (if (<= $n 0) Z (S (fromNum (- $n 1)))))
(= (num2deb $pattern) (map-atom $pattern $x $x))

!(assertAlphaEqualToResult
    (let $newvar (make_var) ($newvar foo))
    (($v foo)))

; A bare top-level declaration (scope:profile ...) applies to the whole
; document, the forms before it included.  Here explicit capture: the lambda
; owns $t although the declaration comes after it.
!(row before)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
(scope:profile explicit-capture per-call reference)
!(row after)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
; The same declaration again agrees with it.
(scope:profile explicit-capture per-call reference)
; A crossing set connects under it.
!(row crossing)
!(let $f (lam z (let (d $t) z ok)){$t} (let $r ($f (d 7)) (Pair $t $r)))
; The program's parse elaborates under it too.
!(row parse)
!(let $c (lift (parse "(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))")) *$c)

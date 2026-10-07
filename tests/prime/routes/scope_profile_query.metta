; A scope profile declared as a query, !(scope:profile ...), follows HE's
; !(pragma! ...) convention: it applies from its position onward, and the
; query itself answers ().  A bare top-level (scope:profile ...) applies to
; the whole document (tests/prime/routes/scope_profile_document.metta).  A
; declaration that names no profile stays as written, as data.  Code formed
; while the program runs from text, by parse, is elaborated under the
; profile in force.
(= (h $x) (unify $x 5 ok no))

; L owns a let's name under mercury-implicit (the default here) and shares
; a name written outside it; explicit capture owns every name.
!(row default)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
!(scope:profile explicit-capture)
!(row after-explicit-capture)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
; Code read from text by parse is elaborated under the profile in force.
!(row parse-explicit-capture)
!(let $c (lift (parse "(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))")) *$c)
!(scope:profile query-wide)
!(row after-query-wide)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(row parse-query-wide)
!(let $c (lift (parse "(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))")) *$c)
; A declaration naming no profile selects nothing.
!(row unknown)
!(scope:profile no-such-profile)
!(row unknown-kept)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(scope:profile mercury-implicit per-call reference)
!(row back)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(row parse-back)
!(let $c (lift (parse "(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))")) *$c)

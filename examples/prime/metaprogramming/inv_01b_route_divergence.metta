; Route agreement for returned values and their structural observation.
;
; The ordinary, prepared-reference, finite-fuel and worker routes retain
; the same residuals, field demands and quoted values. These queries keep
; the former route-divergence cases as regressions. Both expected files
; describe that common result; each argument also crosses `observe`.

(= (observe $v) $v)

; D1. A non-code activation is the residual (unquote v), also as an argument.
;     Resolved: `*` is the evaluator's own on every route (code is opened and
;     elaborated where it is formed), so the residual is the answer on both.
;     default: [(unquote 3)]        fuel: [(unquote 3)]
!(observe *3)
!(observe *(+ 1 2))

; D2. A call no equation matches is data, also as an argument.
;     Both occurrences return (only-one (Crate 1)).
(= (only-one (Box $x)) $x)
!(only-one (Crate 1))
!(observe (only-one (Crate 1)))

; D3. Structural observation demands the pending fields of a constructor
;     argument, including after the identity return: (pair 3 0).
!(pair (+ 1 2) 0)
!(observe (pair (+ 1 2) 0))
;     The pending effect is executed once, yielding (pair () 0).
!(observe (pair (println! "field") 0))

; D4. A parameter both demanded and quoted.  Resolved by the seal: a
;     quotation holds the parameter itself on both routes (renamed apart with
;     the rule at a call), never the argument's source or its value, so the
;     routes agree; observed by the quoted variable's metatype.  Where a
;     quotation captures its argument as written, quote-then-force kept the
;     source on both routes and force-then-quote only on the fuel route
;     (tests/prime/quote_profiles/incoherence.metta).
;     default: [(pair 3 Variable)]  fuel: [(pair 3 Variable)]
(= (quote-then-force $x) (let $q @$x (let $v (+ $x 0) (pair $v $q))))
(= (force-then-quote $x) (let $v (+ $x 0) (pair $v @$x)))
(= (quoted-kind (pair $v (quote $c))) (pair $v (get-metatype $c)))
!(quoted-kind (quote-then-force (+ 1 2)))
!(quoted-kind (force-then-quote (+ 1 2)))

; D5. The pending field built under a variable head retains its demand
;     through the identity return. Both occurrences observe (receipt 3).
(= (shown $c) (receipt $c))
(= (three) 3)
(= (apply-to-three $f) ($f (three)))
!(apply-to-three shown)
!(observe (apply-to-three shown))

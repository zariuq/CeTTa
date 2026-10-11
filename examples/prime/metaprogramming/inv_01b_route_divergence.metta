; Answers that depend on the evaluation route.
;
; With a finite `--fuel` the canonical evaluator runs every query; without it
; the optimized routes (prepared calls, the Need machine, the evaluation
; stack) take the calls they admit.  The two must agree.  On the queries
; below they do not: `inv_01b_route_divergence.expected` is the default
; route and `inv_01b_route_divergence.fuel.expected` is `--fuel 20000`.
; Every query passes an argument through the identity equation `observe`.

(= (observe $v) $v)

; D1. A non-code activation is the residual (unquote v), also as an argument.
;     Resolved: `*` is the evaluator's own on every route (code is opened and
;     elaborated where it is formed), so the residual is the answer on both.
;     default: [(unquote 3)]        fuel: [(unquote 3)]
!(observe *3)
!(observe *(+ 1 2))

; D2. A call no equation matches is data, also as an argument.
;     default: no answer            fuel: [(only-one (Crate 1))]
(= (only-one (Box $x)) $x)
!(only-one (Crate 1))
!(observe (only-one (Crate 1)))

; D3. Fields of a constructor argument.
;     default: [(pair 3 0)]         fuel: [(pair (+ 1 2) 0)]
;     The top-level (pair (+ 1 2) 0) gives (pair 3 0) on both routes.
!(pair (+ 1 2) 0)
!(observe (pair (+ 1 2) 0))
;     default: prints "field", [(pair () 0)]   fuel: [(pair (println! "field") 0)]
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

; D5. A field built under a variable head is printed unforced.
;     default: [(receipt (three))]  fuel: [(receipt 3)]
(= (shown $c) (receipt $c))
(= (three) 3)
(= (apply-to-three $f) ($f (three)))
!(apply-to-three shown)
!(observe (apply-to-three shown))

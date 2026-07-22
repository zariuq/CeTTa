; Prime demand modes are operation contracts.  Strict numeric positions force
; hidden evaluator thunks; source-type and stream positions inspect only what
; they need; explicit suspensions are never silently coerced.

(= (prime-need:demand-four) 4)
(= (prime-need:demand-add-one $x) (+ $x 1))
!(prime-need:demand-add-one (prime-need:demand-four))

; Source-type projection must terminate even though evaluating the source
; computation does not.
(: prime-need:demand-omega (-> Number Number))
(= (prime-need:demand-omega $x) (prime-need:demand-omega $x))
(= (prime-need:demand-type-of $x) (get-type $x))
!(prime-need:demand-type-of (prime-need:demand-omega 0))

; Explicit delay has a public, lower-case type constructor.  It is data for
; an unknown head and a typed contradiction in a Number position.
!(get-type (delay 3))
!(case (get-type delay)
   (((-> $a (suspension $a)) delay-typed)
    ($_ delay-type-mismatch)))
!(case (get-type force)
   (((-> (suspension $a) $a) force-typed)
    ($_ force-type-mismatch)))
!(mystery (delay (prime-need:demand-omega 0)))
!(force ordinary-data)

; The type is usable in ordinary checked declarations.
(: prime-need:takes-suspension
   (-> (suspension Number) Number))
(= (prime-need:takes-suspension $x) 7)
!(prime-need:takes-suspension (delay 3))

; The limit is demanded, while the selected stream is traversed incrementally:
; satisfying the bound prevents evaluation of the later divergent branch.
(= (prime-need:demand-choose $limit $stream)
   (select $limit $stream))
!(prime-need:demand-choose
   (+ 0 1)
   (superpose (A (prime-need:demand-omega 0))))

; A typed strict consumer rejects a suspension instead of forcing it.  Keep
; this negative observation last because an uncaught checked contradiction
; terminates the source file's remaining top-level directives.
!(+ (delay 1) 2)

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

; Shape observers consume a visible expression as syntax data even when its
; first element is callable.  Hidden Prime control is still forced far enough
; to reveal the outer expression.
!(superpose (+ - *))
!(superpose (force (delay (Ops + - *))))

; Complete collection observes a returned field in its selected Need world.
; The comparison earns the numeric value before the constructor returns it;
; exporting the source of that field would expose nested subtraction syntax.
(= (prime-need:bag-value $n)
   (if (== $n -1)
       (println! unreachable-prime-need-bag-value)
       (BagValue $n)))
(= (prime-need:bag-loop $n $acc)
   (if (== $n -1)
       (println! unreachable-prime-need-bag-loop)
       (if (== $n 0) $acc
           (prime-need:bag-loop (- $n 1) (prime-need:bag-value $n)))))
!(collapse (prime-need:bag-loop 3 seed))
!(collect (prime-need:bag-loop 3 seed))

; A produced callable-shaped value remains data under the same collectors.
!(collapse (cons-atom + (1 2)))
!(collect (cons-atom + (1 2)))

; Ordered selection counts observed answers.  Lex and shortlex compare the
; earned values, while reverse preserves reversed occurrence order.
(= (prime-need:ordered-number-loop $n $stop $acc)
   (if (== $n -1)
       (println! unreachable-prime-need:ordered-number-loop)
       (if (== $n $stop) $acc
           (prime-need:ordered-number-loop (- $n 1) $stop (prime-need:bag-value $n)))))
(= (prime-need:ordered-numbers) (prime-need:ordered-number-loop 22 19 seed))
(= (prime-need:ordered-numbers) (prime-need:ordered-number-loop 5 2 seed))
(= (prime-need:ordered-numbers) (prime-need:ordered-number-loop 13 10 seed))
!(collect (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-numbers))
!(select 2 (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-numbers))
!(select (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-numbers))
!(collect (search-policy recursive-dependent-proof (order lex)) (prime-need:ordered-numbers))
!(select 2 (search-policy recursive-dependent-proof (order lex)) (prime-need:ordered-numbers))
!(select (search-policy recursive-dependent-proof (order lex)) (prime-need:ordered-numbers))
!(collect (search-policy recursive-dependent-proof (order shortlex)) (prime-need:ordered-numbers))
!(select 2 (search-policy recursive-dependent-proof (order shortlex)) (prime-need:ordered-numbers))
!(select (search-policy recursive-dependent-proof (order shortlex)) (prime-need:ordered-numbers))

; A single returned row can expose several field alternatives.  A limit of
; one still publishes exactly one occurrence, in the selected world.
(= (prime-need:ordered-tick $x) (chain (println! $x) $u $x))
(= (prime-need:ordered-value $n $x)
   (if (== $n -1) (println! unreachable-prime-need:ordered-value) (BagValue $x)))
(= (prime-need:ordered-field-loop $n $acc $x)
   (if (== $n -1) (println! unreachable-prime-need:ordered-field-loop)
       (if (== $n 0) $acc
           (prime-need:ordered-field-loop (- $n 1) (prime-need:ordered-value $n $x) $x))))
(= (prime-need:ordered-many) (prime-need:ordered-field-loop 3 seed (superpose (1 2))))
(= (prime-need:ordered-many) (prime-need:ordered-field-loop 3 seed (superpose (3 4))))
!(select (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-many))
!(select 3 (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-many))
!(select 2 (search-policy recursive-dependent-proof (order shortlex)) (prime-need:ordered-many))

; Reverse observes only selected fields; lex observes each comparison key.
(= (prime-need:ordered-effects) (prime-need:ordered-field-loop 3 seed (prime-need:ordered-tick first-field)))
(= (prime-need:ordered-effects) (prime-need:ordered-field-loop 3 seed (prime-need:ordered-tick second-field)))
(= (prime-need:ordered-effects) (prime-need:ordered-field-loop 3 seed (prime-need:ordered-tick third-field)))
!(select 2 (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-effects))
!(select (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-effects))
!(select (search-policy recursive-dependent-proof (order lex)) (prime-need:ordered-effects))

; An unselected field may diverge without preventing a reverse prefix.
(= (prime-need:ordered-prefix) (prime-need:ordered-field-loop 3 seed (prime-need:demand-omega 0)))
(= (prime-need:ordered-prefix) (prime-need:ordered-field-loop 3 seed 7))
(= (prime-need:ordered-prefix) (prime-need:ordered-field-loop 3 seed 9))
!(select 2 (search-policy recursive-dependent-proof (order reverse)) (prime-need:ordered-prefix))

; Completed callable-looking data is never activated by ordering.
!(select 2 (search-policy recursive-dependent-proof (order reverse)) (cons-atom + (1 2)))
!(select 2 (search-policy recursive-dependent-proof (order lex)) (cons-atom + (1 2)))
!(select 2 (search-policy recursive-dependent-proof (order shortlex)) (cons-atom + (1 2)))

; A retained tuple can hide its first field behind a Need cell.
; Structural patterns demand that cell while variables leave it alone.
; Returned callable data and quoted bodies retain their value role.

(= (prime-need:head-build (quote (FC $name ())) ((quote $p))) (quote ($name $p)))
(= (prime-need:head-direct ((quote $p))) (quote $p))
(= (prime-need:head-tagged (FC (quote $p))) (quote $p))
(= (prime-need:head-get) (quote a))
(= (prime-need:head-data) (cons-atom + (1 2)))
(= (prime-need:head-code ((+ $x $y))) (Pair $x $y))
(= (prime-need:head-variable ($p)) $p)
(= (prime-need:head-ignore ($p)) ignored)
(= (prime-need:head-lazy $p) (let $tuple (cons-atom $p ()) (prime-need:head-ignore $tuple)))
(= (prime-need:head-loop) (prime-need:head-loop))
(= (prime-need:head-nested ((Node (quote $p)))) (quote $p))
(= (prime-need:head-tick $label) (chain (println! $label) $_ (quote a)))
(= (prime-need:head-pair $p) (Pair (prime-need:head-direct ($p)) (prime-need:head-direct ($p))))
(= (prime-need:head-choice) (quote a))
(= (prime-need:head-choice) (quote a))
(= (prime-need:head-choice) (quote b))
!(prime-need:head-build (quote (FC f ())) ((quote a)))
!(let $p (quote a) (prime-need:head-build (quote (FC f ())) ($p)))
!(let $ps ((quote a)) (prime-need:head-build (quote (FC f ())) $ps))
!(let $p (quote a) (prime-need:head-direct ($p)))
!(chain (quote a) $p (prime-need:head-direct ($p)))
!(let $p (prime-need:head-get) (prime-need:head-direct ($p)))
!(let $p (prime-need:head-get) (prime-need:head-tagged (FC $p)))
!(let $p (prime-need:head-get) (prime-need:head-variable ($p)))
!(let $p (prime-need:head-data) (prime-need:head-code ($p)))
!(let $p (quote (+ 1 2)) (prime-need:head-direct ($p)))
!(let $p (prime-need:head-get) (prime-need:head-nested ((Node $p))))
!(let $p (prime-need:head-tick prime-need:head-effect-once) (prime-need:head-pair $p))
!(prime-need:head-lazy (prime-need:head-loop))
!(prime-need:head-lazy (prime-need:head-tick prime-need:head-unreachable))
!(let $p (quote (prime-need:head-tick prime-need:head-quoted-unreachable)) (prime-need:head-direct ($p)))
!(prime-need:head-pair (prime-need:head-choice))
!(let $p (prime-need:head-data) (Pair (prime-need:head-code ($p)) (eval $p)))

; A computed head keeps its paid argument world.  Both uses share the
; producer, while a returned lambda's body runs for each application.
(= (prime-need:head-apply-twice $f) (Pair ($f 1) ($f 2)))
(= (prime-need:head-apply $f) ($f 3))
!(prime-need:head-pair (prime-need:head-tick prime-need:head-source-once))
!(prime-need:head-apply-twice (lam x (chain (println! prime-need:head-lambda-application) $_ x)))
!(prime-need:head-apply (superpose ((lam x (+ x 1)) prime-need:head-inert (lam x (+ x 1)))))
!(prime-need:head-apply (quote (+ 1 2)))

; Independent pending fields retain their Cartesian choices through the
; ordinary worker handoff and its coordinator-owned observation service.
(= (prime-need:read-service-id $x) $x)
!(prime-need:read-service-id (Pair (superpose (1 2)) (superpose (3 4))))

; Pattern selection exposes only the retained fields it needs. Earned
; callable syntax stays a value until explicit activation.
!(let $box (cons-atom Box ((+ 1 2))) (let (Box $field) $box (Field $field)))
!(let $box (cons-atom Box ((+ 1 2))) (case $box (((Box $field) (Field $field)))))
!(let $box (cons-atom Box ((+ 1 2))) (unify $box (Box $field) (Field $field) NotBox))
!(let $box (cons-atom Box ((+ 1 2))) (case $box (((Box $field) (Field (eval $field))))))
(= (prime-need:pattern-store $tag $value) (add-atom &self (NeedPattern $tag $value)))
(= (prime-need:pattern-box $field) (Box $field))
(= (prime-need:pattern-tick $label $value) (chain (println! $label) $_ $value))
!(prime-need:pattern-store root-wild (prime-need:pattern-tick unreachable-root-field 7))
!(match &self (NeedPattern root-wild $value) (case $value (($unused Kept) (7 Strict))))
!(prime-need:pattern-store field-wild (prime-need:pattern-box (prime-need:pattern-tick unreachable-later-field 7)))
!(match &self (NeedPattern field-wild $value) (case $value (((Box $unused) Kept) ((Box 7) Strict))))
!(prime-need:pattern-store field-choices (prime-need:pattern-box (superpose (7 7 8))))
!(match &self (NeedPattern field-choices $value) (case $value (((Box 7) Strict) ((Box $other) (Fallback $other)))))
!(prime-need:pattern-store field-once (prime-need:pattern-box (prime-need:pattern-tick prime-need:pattern-demand-once 8)))
!(match &self (NeedPattern field-once $value) (case $value (((Box 7) Impossible) ((Box 8) Kept))))
!(case (superpose ()) (($unused Impossible)) Zero)
!(case Empty ((Empty LiteralEmpty) ($unused Impossible)))

; A typed strict consumer rejects a suspension instead of forcing it.  Keep
; this negative observation last because an uncaught checked contradiction
; terminates the source file's remaining top-level directives.
!(+ (delay 1) 2)

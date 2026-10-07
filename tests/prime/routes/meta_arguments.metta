; Meta-arguments.  The reader has two rules: a bracket list or a braces node
; TOUCHING the end of a term, T[...] and T{...}, is the wrapper (meta T [...])
; or (meta T {...}).  With layout before it, a bracket is a list and a brace
; a braces node.  Whitespace matters in the notation only: the core holds the
; attachment explicitly, as the wrapper with the bracket kind and payload,
; and the printer writes it back touching.  The core gives a crossing set
; {...} its reading on a scope-forming construct and a bracket of bindings
; [k := v] its reading on code; every other wrapper is ordinary data, which
; user equations may give a meaning.
(= (observe $v) $v)

; ---- Touching and separated.
!(row touching-braces)
!(let $t (parse "(T{M})") (Pair (size-atom $t) (car-atom $t)))
!(row separated-braces)
!(let $t (parse "(T {M})") (Pair (size-atom $t) (car-atom $t)))
!(row touching-bracket)
!(let $t (parse "(T[M])") (Pair (size-atom $t) (car-atom $t)))
!(row separated-bracket)
!(let $t (parse "(T [M])") (Pair (size-atom $t) (car-atom $t)))
; The wrapper holds the term, then the bracket kind and payload.
!(row wrapper-parts)
!(let (meta $term $payload) (parse "T{M N}") (Pair $term $payload))
!(row wrapper-bracket-parts)
!(let (meta $term $payload) (parse "T[x := a]") (Pair $term $payload))

; ---- Meta-arguments apply left to right.
!(row nested-braces)
!(let (meta (meta $t $a) $b) (parse "T{a}{b}") (Pair $t (Pair $a $b)))
!(row nested-brackets)
!(let (meta (meta $t $a) $b) (parse "T[x := a][y := b]") (Pair $t (Pair $a $b)))
!(row mixed)
!(parse "T{a}[x := b]{c}")

; ---- Layout between a term and a bracket separates them, comments too.
!(row comment-between)
!(parse "(T ; note
{a})")
!(row newline-between)
!(parse "(T
[a b])")

; ---- The printer writes the wrapper back touching, and parse reads what
; the printer wrote back to the same term: the same atom when it holds no
; variable, and the same text again when it holds some.
!(row printed)
!(quote (f x){a b}[k := v])
!(row printed-repr)
!(let $t (parse "@(g $x)[$x := @a][(p q) := (@b @c)]") (repr $t))
!(row roundtrip-ground)
!(let $t (parse "(f x){a b}[k := v]") (== (parse (repr $t)) $t))
!(row roundtrip-ground-nested)
!(let $t (parse "(T{a}{b} [c] {d})") (== (parse (repr $t)) $t))
!(row roundtrip-variables)
!(let $t (parse "@(g $x)[$x := @a][(p q) := (@b @c)]")
   (let $s (repr $t) (let $u (parse $s) (== (repr $u) $s))))
!(row roundtrip-crossing)
!(let $t (parse "(lam z (let $y z (g $y))){$y}")
   (let $s (repr $t) (let $u (parse $s) (== (repr $u) $s))))

; ---- A head the core does not read keeps its wrapper through evaluation
; and printing: uninterpreted, never an error.
!(row unknown-head)
!(foo a){$x}
!(row unknown-head-bracket)
!(bar 1)[q]
!(row unknown-head-held)
!(let $w (foo (+ 1 2)){$x} (Pair $w (size-atom $w)))

; ---- A user-defined head gives its meta-argument a meaning by an equation
; on the wrapper, written touching as the call is.
(= (twice $v){$k} (Pair $v $v $k))
(= (point $x $y)[$axis := $v] (moved $axis $v (point $x $y)))
!(row user-braces)
!(twice 3){$k}
!(row user-bracket)
!(point 1 2)[x := 5]

; ---- A braces node after layout is inert data: never called, its
; elements not evaluated, equal and matching only braces with equal
; elements in order.
!(row braces-inert)
!(let $b {a (+ 1 2)} $b)
!(row braces-inert-argument)
!(Pair {a (+ 1 2)} x)
!(row braces-empty)
!(let $b {} $b)
!(row braces-equal)
!(Pair (== {a b} {a b}) (== {a b} {b a}))
!(row braces-not-expression)
!(== {a b} (a b))
!(row braces-match)
!(let {$x b} {a b} $x)
!(row braces-no-expression-pattern)
!(let ($h $x) {a} (Got $h $x))
!(row braces-case)
!(case {a} ((($h $x) expression) ({$y} (braces $y))))
!(row braces-metatype)
!(get-metatype {a})

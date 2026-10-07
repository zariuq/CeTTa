; The bracket spelling of `lift let`.  A bracket touching the end of a term is
; a meta-argument: C[...] reads as the wrapper (meta C [...]), and the reader
; gives no head a meaning.  The core reads a bracket of bindings on code as
; substitution: C[k := v] is (lift let k v C), a tuple of keys substitutes at
; once, as in let, and so does a list of bindings [(k1 := v1) (k2 := v2)].
; Brackets apply left to right.  A prefix binds tighter than a bracket, so
; @(foo $x)[$x := @a] substitutes into the code @(foo $x).  A slot key may be
; any binder key.  `:=` is a word inside a touching bracket and reserved in a
; list after layout, and brackets end tokens.  A bracket that holds no
; bindings, or that touches what is not code, keeps its wrapper as data.
(= (observe $v) $v)

; ---- Positive: what the reader reads, the wrapper, printed as written.
!(parse "@(foo $x)[$x := @a]")
!(parse "@(f x y)[x := @a][y := @b]")
!(parse "$c[x := @a]")
!(parse "@(pair x y)[(x y) := (@y @a)]")
!(parse "@(pair x y)[(x := @y) (y := @a)]")
; Brackets end tokens: a bracket touching a word wraps it.
!(parse "foo[x := @a]")

; ---- Positive: what it computes, read from this file.
!(observe @(foo $x)[$x := @a])
!(observe @(greet x)[x := @zar])
!(observe @(f x y)[x := @(g y)][y := @b])
!(observe @(pair x y)[(x y) := (@y @a)])
; A list of bindings is one simultaneous substitution, as the tuple is.
!(observe @(pair x y)[(x := @y) (y := @a)])
!(observe @(foo *@(var z))[@(var z) := @a])
!(let $c @(* x 10) (Pair $c[x := @(+ 1 2)] (lift let x @(+ 1 2) $c)))
!(let $c @(* x 10) (let $d $c[x := @(+ 1 2)] *$d))

; ---- Negative: a space before `[` makes it a list, not a meta-argument.
; (The file reader refuses the same sources with their positions:
; tests/support/test_prime_compiled_reader_v1.c.)
!(parse "(f @(foo $x) [a b])")
; In a list `:=` is reserved, so the one-space slip is refused rather than
; read as a list.
!(parse "(f @(foo $x) [$x := @a])")
!(parse "[x := a]")
; A touching bracket that holds no bindings is read, and kept as data: the
; core reads only bindings on code.
!(parse "@(foo $x)[a b]")
!(observe @(foo $x)[a b])
!(observe @(foo $x)[$x = @a])
!(observe @(foo $x)[$x := @a b])
; Bindings touching what is not code keep their wrapper.
!(observe foo[x := @a])
!(observe (Pair 1 2)[x := @a])
; A touching bracket left open is refused.
!(parse "@(foo $x)[$x := @a")

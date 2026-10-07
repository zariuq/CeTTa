; Inferred ownership is carried in the structural code value: a closure's
; own list is part of the term, hidden from the printer, size-atom, patterns
; and `==`, and kept by every semantic operation on terms.  Each row below
; elaborates a closure, operates on it, and applies the result twice: a
; closure that owns $y answers (Pair (g 1) (g 2)); one whose crossing set
; shares $y with its clause answers only when both calls agree.  Printing is
; a text observer: text read back is elaborated afresh (last section).
(= (mk) (lam z (let $y z (g $y))))
(= (mk-shared) (lam z (let $y z (g $y))){$y})
(= (twice $f) (Pair ($f 1) ($f 2)))
(= (once-twice $f) (Pair ($f 1) ($f 1)))
; The closures of cross_form_capture.metta: a closure over the caller's
; argument whose local is spelled $w, as the query spells its own name, or
; $y.
(= (mk-cross) (lam x (lam z (let $w z (Pair $w x)))))
(= (mk-cross2) (lam x (lam z (let $y z (Pair $y x)))))

; ---- Substitution: through let, through a parameter, into code.
!(row subst-let)
!(let $f (mk) (let $g $f (twice $g)))
!(row subst-parameter)
!((lam h (Pair (h 1) (h 2))) (mk))
!(row subst-into-code)
!(let $k (lift (mk)) (let $c (lift let k $k @(Pair (k 1) (k 2))) *$c))
!(row subst-shared)
!(let $f (mk-shared) (let $g $f (twice $g)))
!(row subst-shared-agree)
!(let $f (mk-shared) (let $g $f (once-twice $g)))

; ---- Construction: cons-atom, union-atom, lift app.
!(row cons)
!(let $s (cons-atom (mk) ()) (let $f (car-atom $s) (twice $f)))
!(row union)
!(let $s (union-atom (a) ((mk))) (let $f (index-atom $s 1) (twice $f)))
!(row lift-app)
!(let $k (lift (mk)) (let $c (lift app @Pair (lift app $k @1) (lift app $k @2)) *$c))
!(row cons-shared)
!(let $s (cons-atom (mk-shared) ()) (let $f (car-atom $s) (twice $f)))

; ---- Projection: patterns and sequence operations take the closure out.
!(row project-pattern)
!(let (Box $f) (Box (mk)) (twice $f))
!(row project-index)
!(let $f (index-atom ((mk) other) 0) (twice $f))
!(row project-shared)
!(let (Box $f) (Box (mk-shared)) (twice $f))

; ---- Quotation and opening.
!(row quote-open)
!(let $c (lift (mk)) (let $f *$c (twice $f)))
!(row quote-open-shared)
!(let $c (lift (mk-shared)) (let $f *$c (twice $f)))

; ---- Storage in a space, and matching it out.
!(row stored)
!(let $f (mk) (add-atom &self (stored-closure owned $f)))
!(let $f (mk-shared) (add-atom &self (stored-closure shared $f)))
!(match &self (stored-closure owned $f) (twice $f))
!(row stored-shared)
!(match &self (stored-closure shared $f) (twice $f))

; ---- Code from text: parse elaborates it, as the reader would.
!(row parse)
!(let $c (lift (parse "(lam z (let $y z (g $y)))")) (let $f *$c (twice $f)))
!(row parse-crossing)
!(let $c (lift (parse "(let $y q (lam z (let $y z (g $y))){$y})")) (let $f *$c (twice $f)))

; ---- An imported closure keeps its library's ownership.
!(row imported)
!(import! &self ../scope_spectrum/lib/scope_lib_explicit_capture)
!(let $f (lib-ec) (twice $f))
!(row imported-shared)
!(let $f (lib-ec-shared) (let $r ($f (d 7)) (Pair $t $r)))

; ---- Profile transport: a closure is applied by its own record.
!(scope:profile explicit-capture)
(= (mk-ec) (lam z (let (d $t) z ok)))
!(scope:profile query-wide)
(= (mk-qw) (lam z (let $y z (g $y))))
!(row transport-explicit-capture)
!(let $f (mk-ec) (let $r ($f (d 7)) (Pair $t $r)))
!(row transport-query-wide)
!(let $f (mk-qw) (twice $f))
!(scope:profile mercury-implicit)
!(row transport-back)
!(let $f (mk-qw) (twice $f))
!(let $f (mk) (twice $f))

; ---- Cross-form capture on every path: printed and read back, built as code
; by lift, stored in a space and matched out.  No path may connect the
; closure's local to the query's $w by spelling: each answers (Pair 1 5).
!(row cross-text)
!(let $w 5 (let $u (parse (repr (mk-cross))) (let $c (lift $u) (let $g *$c (($g $w) 1)))))
!(let $w 5 (let $u (parse (repr (mk-cross2))) (let $c (lift $u) (let $g *$c (($g $w) 1)))))
!(row cross-text-applied)
!(let $w 5 (let $g (parse (repr (mk-cross))) (($g $w) 1)))
!(row cross-lift)
!(let $w 5 (let $k (lift (mk-cross)) (let $c (lift app (lift app $k (lift $w)) @1) *$c)))
!(let $w 5 (let $k (lift (mk-cross2)) (let $c (lift app (lift app $k (lift $w)) @1) *$c)))
!(row cross-stored)
!(let $f (mk-cross) (add-atom &self (stored-cross mk $f)))
!(let $f (mk-cross2) (add-atom &self (stored-cross mk2 $f)))
!(let $w 5 (match &self (stored-cross mk $f) (($f $w) 1)))
!(let $w 5 (match &self (stored-cross mk2 $f) (($f $w) 1)))

; ---- Text observers.  The printer shows the authored term: a closure's
; own list is not printed, and a value's crossing set was applied, not kept.
; Text printed and read back is elaborated afresh under the profile in
; force: an owned name is owned again; a shared name not written outside
; the closure's text is owned now.
!(row text-owned)
!(let $f (mk) (let $u (parse (repr $f)) (let $c (lift $u) (let $g *$c (twice $g)))))
!(row text-shared)
!(let $f (mk-shared) (repr $f))
!(let $f (mk-shared) (let $u (parse (repr $f)) (let $c (lift $u) (let $g *$c (twice $g)))))

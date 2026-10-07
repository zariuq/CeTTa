; One convention for every scope-forming construct: braces touching it, its
; crossing set, C{$t ...}, which the reader makes the wrapper
; (meta C {$t ...}).  The set names what the construct shares with the scope
; around it, and decides everything else it holds, under every profile: a
; lambda or a map-atom/foldl-atom template owns every other name its region
; uses, a pattern binder makes every other name of its patterns fresh, and
; every other name of a quotation is the quotation's own.  The profile's
; default applies only where no set is written.  unify refines and takes no
; crossing set: on it the wrapper is data.  Inferred ownership is hidden
; metadata: the printer, size-atom, patterns and `==` see the authored term.
; The document's profile is the default (mercury-implicit per-call
; reference) until the query declarations below.

(= (mk) (lam z (let $y z (g $y))))
(= (mk-shared) (lam z (let $y z (g $y))){$y})
(= (h $x) (unify $x 5 ok no))

; ---- Inferred ownership is hidden: the authored term is what is seen.
!(row hidden-print)
!(mk)
!(row hidden-size)
!(let $l (mk) (size-atom $l))
!(row hidden-pattern)
!(let (lam $p $b) (mk) (Got $p))
!(row hidden-index)
!(let $l (mk) (index-atom $l 0))
!(row hidden-equal)
!(let $l (mk) (== $l $l))
; The ownership is still there: two calls of the closure are independent.
!(row hidden-behaves)
!(let $f (mk) (Pair ($f 1) ($f 2)))

; ---- A crossing set on a lambda: the names it shares, even where the
; default would own them.
!(row lam-override)
!(let $f (lam z (let $y z (g $y))){$y} (Pair ($f 1) ($f 2)))
!(row lam-connect)
!(let $f (lam z (let $y z (g $y))){$y} (Pair ($f 1) $y))
!(row lam-relational)
!(let $f (lam z (let (d $t) z ok)){$t} (let $r ($f (d 7)) (Pair $t $r)))
!(row lam-apply)
!(let $n 1 ((lam x (+ x $n)){$n} 5))
; A closure that shares its clause's name, renamed apart per call.
!(row clause-shared)
!(let $f (mk-shared) (Pair ($f 1) ($f 1)))
!(row clause-shared-conflict)
!(let $f (mk-shared) (Pair ($f 1) ($f 2)))
; The authored wrapper is structure: it is printed and counted as written.
!(row lam-authored)
!(match &self (= (mk-shared) $b) (repr $b))
!(row lam-authored-size)
!(match &self (= (mk-shared) $b) (size-atom $b))
; Its value is the closure, with the set applied.
!(row lam-value)
!(mk-shared)

; ---- The same set on map-atom and foldl-atom templates.
!(row map-default)
!(map-atom (1 2) $e (Pair $e $y))
!(row map-crossing)
!(map-atom (1 2) $e (Pair $e $y)){$y}
!(row map-crossing-bound)
!(let $r (map-atom (1 2) $e (Pair $e $y)){$y} (let $y 5 $r))
!(row fold-crossing)
!(let $r (foldl-atom (1 2) 0 $a $b (+ $a (* $b $k))){$k} (let $k 10 $r))

; ---- A crossing set on a quotation: the names that cross into it.
!(row quote-crossing)
!(let $y 5 (quote (f $x $y)){$y})
!(row quote-sealed)
!(let $y 5 (quote (f $x $y)))
!(row quote-crossing-unified)
!(let $r (h $y) (quote (f $y)){$y})
!(row quote-sealed-unified)
!(let $r (h $y) (quote (f $y)))
; Every other name of a quotation with a crossing set is its own, a hole a
; pattern around it binds included.
!(row quote-crossing-complete)
!(let (quote $x) (quote 5) (Pair (quote (f $x)) (quote (f $x)){}))

; ---- A crossing set on a pattern binder: every other name of its patterns
; is fresh for the part of the form the pattern scopes over.
!(row let-fresh)
!(let $y 1 (Pair $y (let $y 2 $y){}))
!(row let-shared)
!(let $y 1 (Pair $y (let $y 2 $y){$y}))
!(row let-every-construct)
!(let $y 1 (Pair $y (let $y 2 $y){})){}
!(row let-fresh-per-call)
!(let $f (lam z (let $y z (g $y)){}) (Pair ($f 1) ($f 2)))
!(row let-star-fresh)
!(let $y 1 (let* (($y 2) ($z $y)) (Pair $y $z)){})
!(row case-fresh)
!(let $y 1 (case (2 3) ((($y $z) (Got $y $z)))){$z})
; A binder's set may share a name an enclosing binder made fresh: it refers
; to that binder's slot, from inside a lambda too.
!(row shared-outer-fresh)
!(let $y 2 (let $f (lam z (let $y z (Got $y)){$y}) ($f 2))){}
!(row shared-outer-fresh-other)
!(let $y 2 (let $f (lam z (let $y z (Got $y)){$y}) ($f 3))){}
; unify refines and introduces nothing: it takes no crossing set, and the
; wrapper on it is data.
!(row unify-data)
!(unify a a ok no){}

; ---- A written set decides even where rule M would leave a name to an
; enclosing lambda: each lambda written with {} owns its own $y.
!(row nested-override)
!((lam w (Pair ((lam z (let $y z (g $y))){} 1) (let $y w $y))){} 5)
; A template's crossing set is no name written in the scope around: rule M
; leaves $n to the query.
!(row crossing-not-written)
!(let $f (lam a (lam b (Pair a $n)){$n}) (Pair (($f 1) 2) $n))

; ---- A crossing set overrides every profile's default.  The declarations
; below are queries: each applies from its position onward.
!(scope:profile query-wide)
; Query-wide owns nothing by default.  A written set decides: {$y} shares
; $y, so both calls refine one $y; {} owns it, so each call has its own.
!(row qw-default)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(row qw-crossing)
!(let $f (lam z (let $y z (g $y))){$y} (Pair ($f 1) ($f 2)))
!(row qw-crossing-empty)
!(let $f (lam z (let $y z (g $y))){} (Pair ($f 1) ($f 2)))
!(scope:profile lexical-fresh)
; Lexical-fresh gives a let its own slot.  A crossing set on the lambda, or
; on the let, connects the name to the scope around.
!(row lf-default)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) $y))
!(row lf-lam-crossing)
!(let $f (lam z (let $y z (g $y))){$y} (Pair ($f 1) $y))
!(row lf-let-crossing)
!(let $f (lam z (let $y z (g $y)){$y}) (Pair ($f 1) $y))
; A let's crossing set shares its pattern name with the caller.  These
; clauses are read under lexical-fresh, from the declaration above.
(= (second $s $t) (let (d $s $t) (d a b) (Got $t)))
(= (second-crossing $s $t) (let (d $s $t) (d a b) (Got $t)){$t})
!(row lf-second)
!(let $r (second a $t) (Pair $t $r))
!(row lf-second-crossing)
!(let $r (second-crossing a $t) (Pair $t $r))
; A lambda written with a crossing set owns every other name it uses.
!(row lf-lam-owns)
!(let $f (lam z (Pair z $y)){} (let $r ($f 1) (let $y 5 $r)))
; A let's set holds its names in force inside it; every other name of its
; pattern is fresh, even one in force around it.
!(row lf-let-in-force)
!(let $y 1 (let $z 2 (let $y 3 (Pair $y $z)){$z}){$y})
!(scope:profile explicit-capture)
; Explicit capture owns every name its crossing set does not share,
; those its templates share with it included.
!(row ec-default)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
!(row ec-crossing)
!(let $f (lam z (let (d $t) z ok)){$t} (let $r ($f (d 7)) (Pair $t $r)))
!(row ec-shared-up)
!(let $f (lam a (lam b (Pair a $n)){$n}) (Pair (($f 1) 2) $n))
; The lambda owns every name it uses, so its let's set shares the lambda's
; own slot, not the enclosing let's.
!(row ec-shared-outer-fresh)
!(let $y 2 (let $f (lam z (let $y z (Got $y)){$y}) ($f 3))){}
!(scope:profile mercury-implicit per-call reference)
!(row mercury-again)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))

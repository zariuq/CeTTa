; Typed syntax: a `Data` parameter holds its argument as written.
;
; Held syntax stays held through return, deconstruction, construction and
; storage; an equation head may destructure it; explicit `eval` runs it.
; `Atom` is the top type and has no evaluation control: an `Atom` parameter
; evaluates its argument like any other.

(= (f $y) (g $y))
(= (g $y) (pair $y $y))

(: hold (-> (Data Atom) (Data Atom)))
(= (hold $x) $x)
(: tail-of (-> (Data Atom) (Data Atom)))
(= (tail-of $x) (cdr-atom $x))
(: build (-> (Data Atom) (Data Atom)))
(= (build $x) (cons-atom wrap $x))
(: run-held (-> (Data Atom) $r))
(= (run-held $x) (eval $x))

; 1-4. Holding; it survives return, deconstruction and construction.
!(hold ((lam x x) payload))
!(hold (f nil))
!(tail-of (mapf (lam y (f (g y))) nil))
!(build ((lam x x) payload))

; 5-6. Held syntax is an ordinary value for observers.
!(size-atom (hold (f (g nil))))
!(== (hold (f nil)) (hold (g nil)))

; 7-10. An equation head destructures held syntax; the parts stay held, and
;       only explicit evaluation runs them.  No matching head: the call is data.
(: shape (-> (Data Atom) (Data Atom)))
(= (shape (lam $v $b)) (Lambda $b))
(= (shape (Pair $x $y)) (Pair-of $x))
!(shape (lam z (f z)))
!(shape (Pair (f nil) B))
!(shape (Triple A B C))
(: run-first (-> (Data Atom) $r))
(= (run-first (Pair $x $y)) (eval $x))
!(run-first (Pair (f nil) B))

; 11-12. Explicit evaluation runs held code.
!(run-held (f nil))
!(let $d (hold (f nil)) (eval $d))

; 13. `unify` destructures a held value.
!(let $d (hold (Pair (f nil) B)) (unify $d (Pair $x $y) (First $x) none))

; 14-15. Opening a lambda with a variable binder: the body comes back held
;        with the argument in place, and the lambda is untouched (so it can be
;        opened again at another argument).
(: open-at (-> (Data Atom) (Data Atom) (Data Atom)))
(= (open-at (lam $x $body) $v) (let $x $v $body))
!(open-at (lam $y (and (f $y) (g $y))) 0)
!(let $h (lam $y (and (f $y) (g $y)))
   (pair (open-at $h 0) (pair (open-at $h 1) $h)))

; 16-17. Held syntax survives storage.
!(add-atom &self (stored (hold (f nil))))
!(match &self (stored $s) (pair $s (size-atom $s)))

; 18-19. `Atom` is the top type: an Atom parameter evaluates its argument,
;        a plain parameter too.
(: arg-atom (-> Atom Number))
(= (arg-atom $x) (+ $x 1))
!(arg-atom (+ 1 2))
(= (plain-first (pair $x $y)) $x)
!(plain-first (f nil))

; --- `let`, `let*` and `case` patterns take apart a held value ----------------
; Each gives a, as `unify` on the same held value does (query 13).
!(marker gap-1-begin)
!(let (ex $l) (hold (ex a)) $l)
!(let* (($d (hold (ex a))) ((ex $l) $d)) $l)
!(let $d (hold (ex a)) (case $d (((ex $l) $l))))
!(let $d (hold (ex a)) (unify $d (ex $l) $l none))
!(marker gap-1-end)

; --- A held value used as a `match` pattern is the syntax it holds -------------
; Both find the fact.
(fact a)
(: find-held (-> (Data Atom) $r))
(= (find-held $p) (collapse (match &self $p found)))
(= (find-plain $p) (collapse (match &self $p found)))
!(marker gap-3-begin)
!(find-held (fact a))
!(find-plain (fact a))
!(marker gap-3-end)

; --- A declared result type Atom does not hold the body ------------------------
; `Atom` has no evaluation control, so both calls give 3.
(: ret-atom (-> Number Atom))
(= (ret-atom $x) (+ $x 1))
(: ret-number (-> Number Number))
(= (ret-number $x) (+ $x 1))
!(ret-atom 2)
!(ret-number 2)

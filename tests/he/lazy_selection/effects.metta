; Lazy selection in HE's extended profiles, opt-in with
; (pragma! select-mode lazy): a selection of k answers performs only the work
; those answers need, and every effect once.  The answers are those of eager
; selection; only the work after the k-th answer is skipped.

!(pragma! select-mode lazy)

; Four alternatives, each of which writes before it answers.
(= (gen $tag) (let $u (add-atom &self (made $tag 1)) 1))
(= (gen $tag) (let $u (add-atom &self (made $tag 2)) 2))
(= (gen $tag) (let $u (add-atom &self (made $tag 3)) 3))
(= (gen $tag) (let $u (add-atom &self (made $tag 4)) 4))

; 1. Two answers, and the writes of those two alone.
!(select 2 (gen two))
!(collapse (match &self (made two $x) $x))

; 2. Three answers.
!(select 3 (gen three))
!(collapse (match &self (made three $x) $x))

; 3. once: one answer, one write.
!(once (gen one))
!(collapse (match &self (made one $x) $x))

; 4. A let consumes its source value by value: the generator runs only until
;    a value passes the test.
!(once (let $x (gen test) (if (> $x 2) $x (empty))))
!(collapse (match &self (made test $x) $x))

; 5. A match evaluates its template row by row.
(row a)
(row b)
(row c)
!(once (match &self (row $r) (let $u (add-atom &self (made row $r)) $r)))
!(collapse (match &self (made row $x) $x))

; 6. A selection inside a lazy selection keeps its own bound.
!(once (select 2 (gen inner)))
!(collapse (match &self (made inner $x) $x))

; 7. A complete observation inside a bounded one stays complete.
!(once (let $all (collapse (gen bag)) $all))
!(collapse (match &self (made bag $x) $x))

; 8. Without a bound every alternative runs, once each.
!(collapse (gen all))
!(collapse (match &self (made all $x) $x))

; A reordered join cannot publish a pure row before an earlier stored effect
; has been checked. A certificate that later fails cannot retract a prefix.
(lazy-a x)
(lazy-b y1 (add-atom &self lazy-hit))
(lazy-b y2 Ok)
(lazy-c x y2)
(lazy-c x y1)
!(select 1 (match &self (lazy-a $x)
  (match &self (lazy-b $y $body) (match &self (lazy-c $x $y) $body))))
!(collapse (match &self lazy-hit found))
!(remove-atom &self lazy-hit)
!(select 3 (match &self (lazy-a $x)
  (match &self (lazy-b $y $body) (match &self (lazy-c $x $y) $body))))
!(collapse (match &self lazy-hit found))

; A raw function refusal restores its call before bounded publication.
; The unselected occurrence must not perform its effect.
(= (decline-once)
  (function (chain (eval (add-atom &self (decline-seen a))) $r
    (return NotReducible))))
(= (decline-once)
  (function (chain (eval (add-atom &self (decline-seen b))) $r
    (return NotReducible))))
!(once (decline-once))
!(collapse (match &self (decline-seen $x) $x))
!(select 2 (decline-once))
!(collapse (match &self (decline-seen $x) $x))

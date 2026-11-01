; A match whose template branches on comparisons of its solutions' values
; runs the taken branch for the solutions they pass, in place, with their
; values bound: the same answers and effects as testing each solution.
(post 1 a 10)
(post 2 a 20)
(post 3 b 30)
(post 4 b 40)
(post 5 c 50)
(= (in-window $d) (and (>= $d 20) (<= $d 40)))
!(bind! &t (new-space set))
(= (thread $p $who) (let $n (+ $p 100) (let $_ (add-atom &t (tm $who $p $n)) $n)))
!(sort-atom (collapse (match &self (post $p $who $d) (if (in-window $d) (thread $p $who) (empty)))))
!(sort-atom (collapse (match &t (tm $w $p $n) ($w $p $n))))
!(size-atom (collapse (match &self (post $p $who $d) (if (in-window $d) (thread $p $who) (empty)))))
; A taken branch answering several values, and one answering none.
(= (twice $p) (superpose ($p $p)))
!(sort-atom (collapse (match &self (post $p $who $d) (if (> $d 25) (twice $p) (empty)))))
(= (none $p) (empty))
!(collapse (match &self (post $p $who $d) (if (< $d 35) (none $p) (empty))))
; The empty branch first, the test refused.
!(sort-atom (collapse (match &self (post $p $who $d) (if (< $d 30) (empty) (twice $p)))))
; A taken branch reading what earlier runs added.
!(bind! &u (new-space set))
(= (note $p) (let $_ (add-atom &u (seen $p)) (size-atom (collapse (match &u (seen $q) $q)))))
!(sort-atom (collapse (match &self (post $p $who $d) (if (>= $d 30) (note $p) (empty)))))
; Values bound stay values: a symbol naming a function, and an expression
; headed by one, are not evaluated again.
(item 1 double)
(item 2 (double 3))
(item 3 (double 4))
(= (double $x) (* $x 2))
(= (both $v) (pair $v $v))
!(sort-atom (collapse (match &self (item $k $v) (if (> $k 1) (both $v) (empty)))))
!(collapse (match &self (item $k $v) (if (== $k 1) (both $v) (empty))))
; A variable the caller bound selects the solutions.
(= (posts-of $who) (match &self (post $p $who $d) (if (> $d 15) (twice $p) (empty))))
!(sort-atom (collapse (posts-of b)))
!(sort-atom (collapse (posts-of a)))
; A test the native path does not read: a comparison with a float.
!(sort-atom (collapse (match &self (post $p $who $d) (if (> $d 25.5) (twice $p) (empty)))))
; A taken branch redefining the test's own function: the solutions after it
; read the test again, under the new rule, which none passes.
(= (late $d) (> $d 15))
(= (redefine $p)
   (let $_ (remove-atom &self (= (late $d) (> $d 15)))
     (let $_ (add-atom &self (= (late $d) (> $d 1000))) $p)))
!(size-atom (collapse (match &self (post $p $who $d) (if (late $d) (redefine $p) (empty)))))

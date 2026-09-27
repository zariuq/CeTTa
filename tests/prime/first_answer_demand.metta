; `once` computes only what its first answer needs.  An alternative after the
; first answer is never computed, so its effects never run; an alternative
; that fails before the first answer has run its effects exactly once.

; Every level has two alternatives, and the first answer takes the first one
; at each level: only the effects on that path run.
(= (tree 0) leaf)
(= (tree $n)
   (if (> $n 0) (s (let $x (println! (left $n)) (tree (- $n 1)))) (empty)))
(= (tree $n)
   (if (> $n 0) (t (let $x (println! (right $n)) (tree (- $n 1)))) (empty)))
!(once (tree 3))

; A search with 2^40 answers stops at its first one.
(= (gen 0) z)
(= (gen $n) (if (> $n 0) (s (gen (- $n 1))) (empty)))
(= (gen $n) (if (> $n 0) (t (gen (- $n 1))) (empty)))
(= (depth z) 0)
(= (depth (s $x)) (+ 1 (depth $x)))
(= (depth (t $x)) (+ 1 (depth $x)))
!(depth (once (gen 40)))

; An alternative that fails gives way to the next one; the effects it ran are
; not repeated, and the alternative after the answer is not tried.
(= (pick) (let $x (println! (attempt a)) (empty)))
(= (pick) (let $x (println! (attempt b)) b))
(= (pick) (let $x (println! (attempt c)) c))
!(once (pick))

; A `let` takes its source's values one at a time.
(= (num) (superpose (1 2 3)))
(= (first-above-one)
   (let $n (num)
     (if (> $n 1)
         (let $y (println! (accept $n)) $n)
         (let $y (println! (reject $n)) (empty)))))
!(once (first-above-one))

; So does an argument that must be evaluated before the call.
(= (noisy) (let $x (println! (produce 1)) 1))
(= (noisy) (let $x (println! (produce 2)) 2))
(= (plus-ten) (+ 10 (noisy)))
!(once (plus-ten))

; The same holds for `once` inside a function body.
(= (first-tree) (once (tree 2)))
!(first-tree)

; Without a first-answer observer every alternative runs.
!(collapse (pick))

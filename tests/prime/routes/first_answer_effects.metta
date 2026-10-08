; `once` computes only what its first answer needs, on every route: with and
; without --fuel the same effects run, each once, and nothing after the first
; answer is computed.

; Only the effects on the path to the first answer run.
(= (tree 0) leaf)
(= (tree $n)
   (if (> $n 0) (s (let $x (println! (left $n)) (tree (- $n 1))){$x}) (empty)))
(= (tree $n)
   (if (> $n 0) (t (let $x (println! (right $n)) (tree (- $n 1))){$x}) (empty)))
!(once (tree 3))

; A failing alternative gives way to the next; the one after the answer is
; never tried.
(= (pick) (let $x (println! (attempt a)) (empty)))
(= (pick) (let $x (println! (attempt b)) b))
(= (pick) (let $x (println! (attempt c)) c))
!(once (pick))

; A `let` takes its source's values one at a time.
(= (num) (superpose (1 2 3)))
(= (first-above-one)
   (let $n (num)
     (if (> $n 1)
         (let $y (println! (accept $n)) $n){$y}
         (let $y (println! (reject $n)) (empty)){$y})))
!(once (first-above-one))

; So does an argument that must be evaluated before the call.
(= (noisy) (let $x (println! (produce 1)) 1))
(= (noisy) (let $x (println! (produce 2)) 2))
(= (plus-ten) (+ 10 (noisy)))
!(once (plus-ten))

; A search with 2^30 answers stops at its first.
(= (gen 0) z)
(= (gen $n) (if (> $n 0) (s (gen (- $n 1))) (empty)))
(= (gen $n) (if (> $n 0) (t (gen (- $n 1))) (empty)))
(= (depth z) 0)
(= (depth (s $x)) (+ 1 (depth $x)))
(= (depth (t $x)) (+ 1 (depth $x)))
!(depth (once (gen 30)))

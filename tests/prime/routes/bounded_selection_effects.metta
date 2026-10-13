; A selection of k answers computes only what those k answers need, on every
; route: the alternatives after the k-th answer never run, so their effects
; never happen, and a producer with infinitely many answers stops at its
; k-th.  `once` is the selection of one answer.

(= (pick $tag) (let $x (println! ($tag 1)) 1))
(= (pick $tag) (let $x (println! ($tag 2)) 2))
(= (pick $tag) (let $x (println! ($tag 3)) 3))
(= (pick $tag) (let $x (println! ($tag 4)) 4))

; 1. Two answers, and only their effects.
!(select 2 (pick two))

; 2. Three answers.
!(select 3 (pick three))

; 3. A producer with infinitely many answers, each with an effect, stops at
;    its second answer.
(= (from $n) (let $x (println! (step $n)) $n))
(= (from $n) (from (+ $n 1)))
!(select 2 (from 0))

; 4. `once` is the selection of one answer.  A selection in a function body
;    that `once` observes stops at its own bound.
!(once (pick one))
(= (two-of $tag) (select 2 (pick $tag)))
!(once (two-of inner))

; 5. A `let` takes its source's values one at a time, and a choice in a body
;    takes its branches one at a time.
!(select 2 (let $v (pick let) (Got $v)))
(= (branches $tag)
   (superpose ((let $x (println! ($tag a)) a)
               (let $x (println! ($tag b)) b)
               (let $x (println! ($tag c)) c))))
!(select 2 (branches choice))
!(once (branches first-branch))

; 6. Nothing bounds a complete observation: every alternative runs.
!(collapse (pick all))

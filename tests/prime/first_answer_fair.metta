; A first answer of a non-empty bag is a member of it, wherever the members
; lie.  Depth-first descent answers nothing below when the recursive
; alternative comes first; the fair descent answers it, and still reaches a
; deep answer as fast as descent does.  Effects keep their source order.

(= (loop $n) (loop (S $n)))
(= (loop $n) (answer $n))
!(once (loop Z))
!(let $r (once (loop Z)) (Got $r))
!(select 1 (loop Z))

; A named control at the top level runs as it does inside an expression.
!(once (search-policy (control FIFO)) (loop Z))

; A recursion through a space read, around a cycle.  The edge pattern is
; read from the node reached so far, so its `$x` is the parameter's, shared
; with the pattern by the crossing set.
(edge a b)
(edge b a)
(= (reach $x) (match &self (edge $x $y) (reach $y)){$x})
(= (reach $x) (at $x))
!(once (reach a))

; The first answer lies deep: every member is thirty levels down.
(= (gen 0) z)
(= (gen $n) (if (> $n 0) (s (gen (- $n 1))) (empty)))
(= (gen $n) (if (> $n 0) (t (gen (- $n 1))) (empty)))
(= (depth z) 0)
(= (depth (s $x)) (+ 1 (depth $x)))
(= (depth (t $x)) (+ 1 (depth $x)))
!(depth (once (gen 30)))

; A choice written out whose first branch never answers.  A pure choice is
; observed as one expression, so the branch that loops starves nothing.
(= (spin) (spin))
!(once (superpose ((spin) (answer x))))
!(once (hyperpose ((spin) (answer x))))
!(select 2 (superpose ((spin) (answer x) (answer y))))
!(select 2 (hyperpose ((spin) (answer x) (answer y))))

; A choice with effects is taken one branch at a time, in source order: the
; second branch, and its effect, are never reached.
!(once (superpose ((answer a) (let $u (println! never) (answer b)))))

; A producer with effects runs in source order: only the first path's
; effects happen.
(= (tree 0) leaf)
(= (tree $n) (if (> $n 0) (s (let $x (println! (left $n)) (tree (- $n 1)))) (empty)))
(= (tree $n) (if (> $n 0) (t (let $x (println! (right $n)) (tree (- $n 1)))) (empty)))
!(once (tree 2))

; A divergent register-loop alternative that grows its argument yields to a
; sibling answer at the inline quantum and under fair native control.
(= (cf $n) (cf (+ $n 1)))
!(once (superpose ((cf 0) (answer cf))))
!(select 2 (superpose ((cf 0) (answer cf) (answer cf2))))

; Deep single-path recursion whose leaf matches two equations that both
; demand the accumulated argument adopts the inline frontier and shares the
; in-flight force across both equations.
(= (lookup a) 1)
(= (lookup b) 2)
(= (lookup c) 3)
(= (sum3) (+ (lookup a) (+ (lookup b) (lookup c))))
(= (rep 0 $x) $x)
(= (rep $n $x) (if (> $n 0) (rep (- $n 1) (+ $x (sum3))) $x))
!(once (rep 3000 0))
!(select 2 (rep 3000 0))

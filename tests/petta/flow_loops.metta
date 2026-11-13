; Imperative flows (lib_flow): sequences, loops over a collection, over the
; matches in a store, while a condition holds, and until a step is quiet.
!(import! &self (library lib_flow))
!(import! &self (library lib_transform))

; A sequence runs each statement to completion and drops its answers; the
; last statement gives the answers.
!(flow:then (println! first) (flow:then (println! second) last))
; A statement with two answers runs once to completion: one answer, not two.
!(collapse (flow:then (superpose (1 2)) done))
; Contrast: a let chain continues once per answer of the bound statement.
!(collapse (let $_ (superpose (1 2)) done))

; A loop over the answers of a computation, in order: superpose holds its
; list and runs each item, so the list is never a call of a.
!(flow:for-each (superpose (a b c)) (|-> ($x) (println! (item $x))))
; No answers: the body never runs.
!(flow:for-each (superpose ()) (|-> ($x) (println! (never $x))))
; A computation with two answers: the body runs once per answer, and the
; loop itself answers once.
!(collapse (flow:for-each (superpose ((p q) (r))) (|-> ($x) (println! (each $x)))))
; Contrast: the loop's computation runs, so a bare (twice 3) is a call.
(= (twice $x) (* 2 $x))
!(flow:for-each (twice 3) (|-> ($x) (println! (got $x))))

; A loop over the matches in a store.
!(bind! &store (new-space))
!(add-atom &store (job j1 3))
!(add-atom &store (job j2 5))
!(flow:for-match &store (job $j $c) (println! (cost $j $c)))
; No match: the body never runs.
!(flow:for-match &store (task $t) (println! (never $t)))

; A loop while a condition holds: the condition is evaluated before each round.
!(bind! &n (new-state 0))
!(flow:while (< (get-state &n) 3) (change-state! &n (+ 1 (get-state &n))))
!(get-state &n)
; A condition that is false at once runs nothing.
!(flow:while (> (get-state &n) 10) (println! never))
; A condition with several answers holds when one of them is True.
!(bind! &m (new-state 0))
!(flow:while (superpose (False (< (get-state &m) 2))) (change-state! &m (+ 1 (get-state &m))))
!(get-state &m)

; A loop until a step is quiet: here a transform that moves one token a round.
!(bind! &tokens (new-space))
!(add-atom &tokens (token a))
!(add-atom &tokens (token b))
!(add-atom &tokens (token c))
(= (move-one) (transform &tokens (once (take (token $t))) (moved $t) &tokens))
!(flow:until-quiet (move-one))
!(sort-atom (collapse (match &tokens (moved $t) $t)))
!(collapse (match &tokens (token $t) $t))

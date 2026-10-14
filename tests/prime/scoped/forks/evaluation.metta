; FORK-EVAL: shared demand or fresh demand?  One nondeterministic value read
; twice: is it one choice observed twice, or two independent draws?

(= (coin) (superpose (H T)))
; Shared: the binding is one demand, both reads see the same choice.
!(let $c (coin) (Pair $c $c))
; Fresh: two occurrences of the producer are two independent draws.
!(Pair (coin) (coin))
; A first-class context shares a lazily bound choice across reads.
!(let $ctx (ctx:bind ctx:empty choice (coin))
   (Pair (ctx:get $ctx choice) (ctx:get $ctx choice)))
; Explicit suspension makes the sharing visible: one delay, forced twice,
; is one choice.
!(let $d (delay (coin)) (Pair (force $d) (force $d)))

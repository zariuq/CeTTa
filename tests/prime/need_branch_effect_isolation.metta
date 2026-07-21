; Prime state effects are branch-local deltas.  Both siblings inherit the
; same parent value; neither mutates the ambient cell without an explicit
; commit policy.
!(bind! &c (new-state 0))

(= (bump-choice)
   (let $x (superpose (A B))
     (let $old (get-state &c)
       (let $_ (change-state! &c (+ $old 1))
         (Pair $x $old)))))

!(let $p (delay (bump-choice))
   (force $p))
!(get-state &c)

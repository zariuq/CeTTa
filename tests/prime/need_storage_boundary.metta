; Persistent spaces store ordinary Prime syntax, never evaluator-private
; thunk references, and crossing the boundary is reification rather than
; forcing.
!(bind! &prime-need:stored (new-space))
!(bind! &prime-need:store-counter (new-state 0))
(= (prime-need:store-bump)
   (let $old (get-state &prime-need:store-counter)
     (let $_ (change-state! &prime-need:store-counter (+ $old 1))
       $old)))
(= (prime-need:store-unused $x)
   (let $_ (add-atom &prime-need:stored (Stored $x)) stored))

!(prime-need:store-unused (prime-need:store-bump))
!(get-state &prime-need:store-counter)
!(match &prime-need:stored (Stored (prime-need:store-bump)) found-source)
!(match &prime-need:stored (Stored (__prime_need_ref_v1 $s $t)) leaked-ref)

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
; Storage reifies the source without running the pending effect. Ordinary
; return subsequently observes its value. A declared Data view retrieves
; the stored source without executing it; neither view exposes heap IDs.
!(match &prime-need:stored (Stored $saved) $saved)
(: prime-need:stored-code (-> (Data Atom) (Data Atom)))
(= (prime-need:stored-code $value) $value)
!(match &prime-need:stored (Stored $saved) (prime-need:stored-code $saved))
!(match &prime-need:stored (Stored (__prime_need_ref_v1 $s $t)) leaked-ref)

; An explicit suspension remains explicit across the same data boundary.
!(add-atom &prime-need:stored
   (StoredExplicit (delay (prime-need:store-bump))))
!(match &prime-need:stored (StoredExplicit $saved) $saved)
!(get-state &prime-need:store-counter)

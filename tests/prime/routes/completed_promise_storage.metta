; An explicit promise keeps its identity after its ready result is stored.
; Reusing it in the selected world shares its cache and branch-local effect.
!(bind! &counter (new-state 0))
(= (bump)
   (let $old (get-state &counter)
     (let $_ (change-state! &counter (+ $old 1)) $old)))
!(let $promise (delay (bump))
   (let $first (force $promise)
     (let $_ (bind! &completed $promise)
       (Checks $first (force &completed) (get-state &counter)))))
!(get-state &counter)

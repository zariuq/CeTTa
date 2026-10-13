; Names produced at runtime retain Symbol semantics without a permanent ID.
!(let $name (atom_concat registry_gc_ state)
   (progn (change-state! $name 21) (get-state $name)))
!(let $name (atom_concat registry_gc_ space)
   (progn (add-atom $name (row 41))
          (collapse (match $name (row $v) $v))))
; A later callable declaration cannot hide the previously bound space.
!(let $name (atom_concat registry_gc_ late)
   (progn (add-atom $name (row 7))
          (add-atom &self (= ($name) 97))
          ((collapse (match $name (row $v) $v)) (eval ($name)))))
; Branch-local globals disappear with their registry. The name bytes and read
; result must nevertheless survive each active worker's evaluation.
!(msort (collapse (hyperpose
    ((let $name (atom_concat registry_gc_worker_ left)
       (progn (change-state! $name 11) (get-state $name)))
     (let $name (atom_concat registry_gc_worker_ right)
       (progn (change-state! $name 12) (get-state $name)))))))
!(let $name (atom_concat registry_gc_ transaction)
   (progn (change-state! $name 61)
          (transaction (change-state! $name 62))
          (get-state $name)))

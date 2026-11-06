; A declaration whose formal is a state cell: (: f (-> $formal R)) with
; (: a $actual), both cells holding 1, types (f a) as R; once the formal's
; cell holds 2 it does not, though no declaration changed.  The store keeps
; no typing for this space, so the update is checked afresh.
!(let $formal (new-state 1)
   (let $actual (new-state 1)
     (let $added-a (add-atom &self (: a $actual))
       (let $added-f (add-atom &self (: f (-> $formal R)))
         (let $target (new-state (f a))
           (let $changed (change-state! $formal 2)
             (change-state! $target (f a))))))))

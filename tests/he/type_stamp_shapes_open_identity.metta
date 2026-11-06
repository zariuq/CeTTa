; Declarations written as open rows, ($d a $cell), can answer the type query
; with state cells too.  A space with an open row keeps no typing for any
; expression, and adding the row invalidates what was kept before it.
(: h H)
!(let $t1 (new-state 1)
   (let $t2 (new-state 1)
     (let $added-a (add-atom &self ($d1 a $t1))
       (let $added-b (add-atom &self ($d2 b $t2))
         (let $added-c (add-atom &self ($d3 c $t2))
           (let $warm-a (new-state (h a))
             (let $warm-b (new-state (h b))
               (let $target (new-state (h b))
                 (let $changed (change-state! $t1 2)
                   (change-state! $target (h c)))))))))))

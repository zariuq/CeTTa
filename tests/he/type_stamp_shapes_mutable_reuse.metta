; Declarations holding state cells: (: a $t1) and (: b $t2), (: c $t2),
; both cells holding 1.  A typing computed through such a row reads the
; cell's current contents, so the store keeps none for this space; one kept
; would answer the typing of (h b) for (h c) after $t1 changes.
(: h H)
!(let $t1 (new-state 1)
   (let $t2 (new-state 1)
     (let $added-a (add-atom &self (: a $t1))
       (let $added-b (add-atom &self (: b $t2))
         (let $added-c (add-atom &self (: c $t2))
           (let $warm-a (new-state (h a))
             (let $warm-b (new-state (h b))
               (let $target (new-state (h b))
                 (let $changed (change-state! $t1 2)
                   (change-state! $target (h c)))))))))))

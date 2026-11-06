; A row with a variable head can match HE's subtype query, as
; ($row-head (H N) (H S)) matches (:< (H N) (H S)).  Adding one changes the
; typings that query reads, so it counts as a declaration change: the store
; keeps no typing from before it.
(: h H)
(: a N)
(: b S)
(:< Unrelated Top)
!(let $warm (new-state (h a))
   (let $target (new-state (h b))
     (let $added (add-atom &self ($row-head (H N) (H S)))
       (change-state! $target (h a)))))

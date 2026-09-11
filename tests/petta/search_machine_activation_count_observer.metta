; A collection observation must survive the source-plus-environment
; representation used for equation activation.  Duplicate source occurrences
; retain their multiplicity.
(activation-count-source alpha left)
(activation-count-source alpha left)
(activation-count-source alpha right)
(activation-count-source beta only)

(= (activation-count-items $key)
   (collapse
     (match &self
            (activation-count-source $key $value)
            $value)))

; Direct let and sequential let* are two presentations of the same
; count-consuming dataflow.  The second producer below depends on a binding
; established earlier in the same activation.
(= (activation-count-direct $key)
   (let $items (activation-count-items $key)
        (counts (length $items)
                (size-atom $items)
                (length $items))))

(= (activation-count-sequential $key)
   (let* (($forwarded $key)
          ($items (activation-count-items $forwarded)))
         (counts (length $items)
                 (size-atom $items)
                 (length $items))))

; Observing the collection as well as its cardinality rejects contraction and
; preserves the complete occurrence bag.
(= (activation-count-materialized $key)
   (let* (($items (activation-count-items $key)))
         (pair (length $items) (superpose $items))))

!(activation-count-direct alpha)
!(activation-count-sequential alpha)
!(activation-count-sequential missing)
!(activation-count-materialized alpha)

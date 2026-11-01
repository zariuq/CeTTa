; Data-preserving selection. Returning a selected atom as an expression may
; interpret Boolean-like values; the constructor keeps that value as data.
(: atom-data:item (-> Atom Number %Undefined%))
(= (atom-data:item $items $position)
  (if (== (get-metatype $items) Expression)
    (if (>= $position 0) (atom-data:item-walk $items $position) (AtomDataRefusedV1 position))
    (AtomDataRefusedV1 expression)))
(= (atom-data:item-walk $items $position)
  (if (== $items ()) AtomDataMissingV1
    (let ($head $tail) (decons-atom $items)
      (if (== $position 0) (AtomDataV1 $head)
        (if (> $position 0) (atom-data:item-walk $tail (- $position 1)) (AtomDataRefusedV1 position))))))

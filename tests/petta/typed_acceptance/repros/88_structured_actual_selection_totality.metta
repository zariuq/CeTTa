; A polymorphic cardinality call receives a tagged tuple whose fields remain
; variables.  Its outer constructor proves the sole clause is selected, so a
; deterministic continuation remains deterministic.

(: keep-cell (-[det]-> Number Number))
(= (keep-cell $value) $value)

(: select-cell
   (-[$effect]-> (Cell Number (List Number))
                 (-[$effect]-> Number Number) Number))
(= (select-cell (Cell $head $tail) $continuation)
   ($continuation $head))

(: cell-shape-caller (-[det]-> Number (List Number) Number))
(= (cell-shape-caller $head $tail)
   (select-cell (Cell $head $tail) keep-cell))

!(test (cell-shape-caller 1 (2 3)) 1)

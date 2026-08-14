; Expression is an open selector domain: atoms and other values exist besides
; the empty and nonempty list shapes.  Even with a deterministic closure, the
; two clauses below therefore cannot justify a deterministic caller.

(: keep-expression-tag (-[det]-> Number Number))
(= (keep-expression-tag $tag) $tag)

(: select-expression-shape
   (-[$effect]-> Expression (-[$effect]-> Number Number) Number))
(= (select-expression-shape () $continuation)
   ($continuation 0))
(= (select-expression-shape (cons $head $tail) $continuation)
   ($continuation 1))

(: expression-shape-caller (-[det]-> Expression Number))
(= (expression-shape-caller $value)
   (select-expression-shape $value keep-expression-tag))

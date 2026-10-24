!(import! &self tensor)

; A packed tensor callback absorbs element by element: an element whose
; first operand is False never reaches the guard, which would diverge
; there; an element whose first operand is True demands it.  The default
; and reference routes run the packed callback (tests/support/
; check_tensor_bulk.py checks that admission), the canonical evaluator the
; authored map; they agree.
(= (loop) (loop))
(= (positive $x) (if (> $x 0.0) True (loop)))
(= (packed-and $x) (if (and (> $x 0.0) (positive $x)) 1.0 0.0))
(= (packed-or $x) (if (or (<= $x 0.0) (positive $x)) 1.0 0.0))
!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (4) (-1.0 0.0 2.0 3.0))) packed-and ()))
!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (4) (-1.0 0.0 2.0 3.0))) packed-or ()))

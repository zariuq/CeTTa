; Declared arrow signatures on the leaf callables make the prepared lanes
; decline the whole program (inner calls hit declared heads), so the machine
; and its specializer run; Number parameters keep every answer unchanged.
(: route-inc (-> Number Number))
(: route-double (-> Number Number))
(= (route-inc $x) (+ $x 1))
(= (route-double $x) (* $x 2))

(= (route-map $f ()) ())
(= (route-map $f (cons $x $xs))
   (cons ($f $x) (route-map $f $xs)))

; Repeated calls reuse the specialization selected by the callable argument.
!(route-map route-inc (1 2 3))

; A different callable selects a different derived head.
!(route-map route-double (1 2 3))

; Returning to the first callable must not inherit the second route.
!(route-map route-inc (4 5))

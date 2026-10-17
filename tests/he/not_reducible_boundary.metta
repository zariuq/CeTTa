; An equation whose body is NotReducible leaves its call unreduced: the
; call is the value, as when no equation applies.  The marker is consumed
; where the call's equation results are taken, so it never reaches a caller.
(= (freeze $x) NotReducible)
(= (bar) NotReducible)
(= (freeze2 $x) (bar))
(= (half $x) (if (== $x 4) 2 NotReducible))
(: freeze3 (-> Number Number))
(= (freeze3 $x) NotReducible)
(= (multi) NotReducible)
(= (multi) val)
(= (thaw $x) thawed)
(= (sel A) NotReducible)
(= (sel B) bee)
(= (id $x) $x)
(= (fst (pair $a $b)) $a)

!(freeze 7)
!(freeze (+ 1 6))
!(freeze3 7)
; The innermost call whose body is the marker stays: here bar, not freeze2.
!(freeze2 7)
; if is defined by equations: a NotReducible branch leaves the if.
!(half 4)
!(half 5)
!(if True NotReducible 1)
; Each equation is an alternative of its own.
!(multi)
; The unreduced call is a value like any other.
!(let $r (freeze 7) (raw $r))
!(collapse (freeze 7))
; Minimal instructions see the marker itself, and data keeps it.
!(function (chain (eval (freeze 7)) $r (return (raw $r))))
!(function (return NotReducible))
!(foo NotReducible)
!(thaw 7)
; An open call takes the bindings of the equation that leaves it unreduced.
!(sel $z)
!(let $v (sel $w) (wrap $v))
; The marker reaches a body through an argument too.
!(fst (pair NotReducible 1))

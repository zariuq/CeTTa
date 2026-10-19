; chain binds its destination in its template.  A binding that the nested
; computation makes for a variable spelled like the destination stays
; outside the template; any other binding reaches it.
(= (pick) A)
(= (pick) B)

; The destination is the binder: the result, not the nested binding.
!(function (chain (unify $x A B refused) $x (return $x)))
!(chain (unify $x A B refused) $x $x)
; Another destination: the nested binding reaches the template.
!(function (chain (unify $x A B refused) $r (return (pair $r $x))))
!(function (chain (unify $p P ok fail) $u
  (chain (eval (pick)) $r (return (pair $r $p)))))
; An inner chain's destination stays its binder when an outer binding
; names the same variable, directly or inside a function.
!(function (chain (unify $x A B refused) $y
  (chain (unify C C D refused) $x (return (pair $x $y)))))
!(function (chain (unify $x A ok refused) $u
  (chain (function (chain (unify C C D refused) $x (return $x))) $v
    (return (pair $v $x)))))
; Results arrive one by one, each with its own bindings.
!(function (chain (eval (pick)) $x
  (chain (unify $x A yes no) $r (return (pair $x $r)))))
; A result that is the bound value itself.
!(function (chain (unify $x A $x refused) $x (return $x)))
; Substitution is simultaneous: a result that mentions the destination
; keeps that occurrence.
!(function (chain (unify $y (f $x) $y refused) $x (return $x)))

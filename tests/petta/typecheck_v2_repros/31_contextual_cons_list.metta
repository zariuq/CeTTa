; A cons used where List Number is required checks its head as Number and its
; tail as List Number.  This recursive call exercises both halves of that
; contextual rule.

(: last-number (-[semidet]-> (List Number) Number))
(= (last-number (cons $x ())) $x)
(= (last-number (cons $_x (cons $next $rest)))
   (last-number (cons $next $rest)))

!(test (last-number (cons 1 (cons 2 ()))) 2)

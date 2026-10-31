; The residual switch as a pragma: it applies to what is evaluated after it.

(: foo (-> Number Number))
(= (late-let $n $m) (let $r (foo $n) (let $u (add-atom &self (= (foo $n) $m)) $r)))

; The default: the residual is evaluated again, after its equation arrived.
!(late-let 5 50)

!(pragma! prime-eval-residuals completed)

; Completed: the residual is read as data.
!(late-let 6 60)

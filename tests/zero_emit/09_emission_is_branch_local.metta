(seed a)
(seed b)
(let $x
  (match &self (seed $x) $x)
  (let $_
    (emit &self (derived $x))
    (match &self (derived $y) (seen $x $y))))

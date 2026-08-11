(fact a)
(fact b)
(let $x
  (match &self (fact $x) $x)
  (quote (seen $x)))

(let (wanted $x)
  (quote (other a))
  (emit &self (should-not-appear a)))

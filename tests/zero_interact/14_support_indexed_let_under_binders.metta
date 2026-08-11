(let (Lam A $body)
  (quote (Lam A (idx 0)))
  (quote (Lam A (Lam A $body))))

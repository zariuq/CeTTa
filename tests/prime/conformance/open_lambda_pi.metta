; Stable open lambda-Pi intake: synthesis, checking, beta-eta conversion,
; and a decisive negative scope witness through the public Prime judgment.
!(prime-judge &self
  (Synth (PrimeScoped PrimeCtxNil (Lam U0 (idx 0)))))
!(prime-judge &self
  (Check (PrimeScoped PrimeCtxNil (Lam U0 (idx 0))) (Pi U0 U0)))
!(prime-judge &self
  (Convert
    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil)
      (App (Lam U0 (idx 0)) (idx 0)))
    (PrimeScoped (PrimeCtxCons U0 PrimeCtxNil) (idx 0))))
!(prime-judge &self
  (Convert
    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil)
      (Lam U0 (App (idx 1) (idx 0))))
    (PrimeScoped (PrimeCtxCons (Pi U0 U0) PrimeCtxNil) (idx 0))))
!(prime-judge &self
  (Synth (PrimeScoped PrimeCtxNil (idx 0))))

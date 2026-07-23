; Honest budgets: an out-of-fuel machine says "I ran out",
; never "there was nothing there".
;
; lts:rho:cost:causal-prefix takes two separate allowances -- reaction
; fuel and exact-cover search work -- and returns
; (lts:rho:cost:prefix STATUS RECEIPT) with a status that never lies:
;
;   quiescent         only after exhaustive search PROVES no funded
;                     successor exists,
;   fuel-exhausted    the firing allowance ended first,
;   search-exhausted  cover search stopped while further search remained
;                     semantically relevant.
;
; Run: ./cetta examples/rho/cost/budget_meter.metta

!(import! &self lts:rho:cost)

; One funded handshake: with fuel it fires and quiescence is then proven.
!(println! "Funded pair, ample allowances -> fires, then proven quiescent:")
!(lts:rho:cost:causal-prefix 5 1000
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv desk $m (rho:cost:signed rho:nil stamped))
        (rho:send desk (rho:cost:signed rho:nil form)))
      clerk)
    (rho:cost:purse desk
      (rho:cost:stack-cons clerk rho:cost:stack-empty))))

; The same pair with zero fuel: no search is even attempted, and the
; machine reports only that its firing allowance ended.
!(println! "Same pair, zero fuel -> fuel-exhausted, untouched state:")
!(lts:rho:cost:causal-prefix 0 1000
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv desk $m (rho:cost:signed rho:nil stamped))
        (rho:send desk (rho:cost:signed rho:nil form)))
      clerk)
    (rho:cost:purse desk
      (rho:cost:stack-cons clerk rho:cost:stack-empty))))

; A tiny search allowance ends inside the cover search: that is reported
; as search exhaustion, distinct from both other verdicts, because an
; incomplete search proves nothing about quiescence.
!(println! "Same pair, search allowance 1 -> search-exhausted, not quiescent:")
!(lts:rho:cost:causal-prefix 1 1
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv desk $m (rho:cost:signed rho:nil stamped))
        (rho:send desk (rho:cost:signed rho:nil form)))
      clerk)
    (rho:cost:purse desk
      (rho:cost:stack-cons clerk rho:cost:stack-empty))))

; Why search deserves its own meter: funding selection is exact cover, and
; cover families multiply.  A demand for one `coin` over two purses with
; different stacks has two genuinely different futures -- the spend is the
; same, the remaining tills differ.  The exhaustive frontier keeps both.
!(println! "Two covers for one spend -> two successors, never truncated:")
!(lts:rho:cost:steps
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv desk $m (rho:cost:signed rho:nil stamped))
        (rho:send desk (rho:cost:signed rho:nil form)))
      coin)
    (rho:cost:purse desk
      (rho:cost:stack-cons coin rho:cost:stack-empty))
    (rho:cost:purse desk
      (rho:cost:stack-cons coin
        (rho:cost:stack-cons coin rho:cost:stack-empty)))))

; Notary chain: a receipt is a causal record, not a counter.
;
; A three-stage supply chain runs on one channel `market`: the farm ships
; beans, the roaster roasts them, the cafe brews the cup.  Each stage is a
; funded reaction whose continuation contains the next stage, and each
; stage's spend is drawn from the purse on `market` itself.
;
; lts:rho:cost:causal-trace runs the chain to quiescence and returns a
; receipt: one event per reaction, in order.  Each event carries
;   - a fresh run-local id,
;   - a cause list: the ids of the events that PRODUCED the exact
;     occurrences this event consumed (repetitions kept -- consuming a
;     continuation and a purse tail both produced by event 0 yields (0 0)),
;   - one funding record per token spent (purse location, head signature),
;   - the raw consumed signature.
; The cause lists below chain 0 <- 1 <- 2: the receipt IS the provenance
; audit trail.  Totals, prices, and depths are computed from it afterward;
; nothing in the run is a mere counter.
;
; Run: ./cetta examples/rho/cost/notary_chain.metta

!(import! &self lts:rho:cost)

!(println! "Three funded stages, one channel, one purse; the receipt")
!(println! "records who consumed what from whom:")

!(lts:rho:cost:causal-trace
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv market $beans
          (rho:cost:signed
            (rho:par
              (rho:recv market $roast
                (rho:cost:signed
                  (rho:par
                    (rho:recv market $cup
                      (rho:cost:signed rho:nil espresso))
                    (rho:send market
                      (rho:cost:signed rho:nil brewed)))
                  cafe))
              (rho:send market
                (rho:cost:signed rho:nil roasted)))
            roaster))
        (rho:send market
          (rho:cost:signed rho:nil harvest)))
      farm)
    (rho:cost:purse market
      (rho:cost:stack-cons farm
        (rho:cost:stack-cons roaster
          (rho:cost:stack-cons cafe rho:cost:stack-empty))))))

; The same machine honestly refuses to invent provenance: two unrelated
; reactions on separate channels get empty cause lists even though one
; event id is emitted before the other.  Order of emission is not
; causality.

!(println! "Two independent settlements: no false arc between them:")

!(lts:rho:cost:causal-trace
  (rho:cost:par
    (rho:cost:signed
      (rho:par
        (rho:recv spot $m (rho:cost:signed rho:nil spot-done))
        (rho:send spot (rho:cost:signed rho:nil spot-pay)))
      trader-a)
    (rho:cost:signed
      (rho:par
        (rho:recv forward $m (rho:cost:signed rho:nil forward-done))
        (rho:send forward (rho:cost:signed rho:nil forward-pay)))
      trader-b)
    (rho:cost:purse spot
      (rho:cost:stack-cons trader-a rho:cost:stack-empty))
    (rho:cost:purse forward
      (rho:cost:stack-cons trader-b rho:cost:stack-empty))))

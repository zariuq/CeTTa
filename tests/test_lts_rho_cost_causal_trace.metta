!(import! &self lts:rho:cost)

; A continuation and the selected purse tail are both outputs of event 0.
; Consuming both at event 1 therefore retains two occurrence arcs.
!(assertEqualToResult
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv chain $message
            (rho:cost:signed
              (rho:par
                (rho:recv chain $next
                  (rho:cost:signed rho:nil done))
                (rho:send chain
                  (rho:cost:signed rho:nil second-payload)))
              spend))
          (rho:send chain
            (rho:cost:signed rho:nil first-payload)))
        spend)
      (rho:cost:purse chain
        (rho:cost:stack-cons spend
          (rho:cost:stack-cons spend rho:cost:stack-empty)))))
  ((lts:rho:cost:receipt
    ((lts:rho:cost:event
       0
       ()
       ((lts:rho:cost:funding chain spend))
       spend)
     (lts:rho:cost:event
       1
       (0 0)
       ((lts:rho:cost:funding chain spend))
       spend))
    (rho:cost:par
      (rho:cost:purse chain rho:cost:stack-empty)
      (rho:cost:signed rho:nil done)))))

; Split-endpoint COMM retains one arc for each produced endpoint occurrence and
; one more for the produced purse tail selected to fund it.
!(assertEqualToResult
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv split $message
            (rho:cost:par
              (rho:cost:signed
                (rho:recv split $next
                  (rho:cost:signed rho:nil split-done))
                recv-spend)
              (rho:cost:signed
                (rho:send split
                  (rho:cost:signed rho:nil split-payload))
                send-spend)))
          (rho:send split
            (rho:cost:signed rho:nil seed-payload)))
        seed-spend)
      (rho:cost:purse split
        (rho:cost:stack-cons seed-spend
          (rho:cost:stack-cons
            (rho:cost:sig-mul recv-spend send-spend)
            rho:cost:stack-empty)))))
  ((lts:rho:cost:receipt
    ((lts:rho:cost:event
       0
       ()
       ((lts:rho:cost:funding split seed-spend))
       seed-spend)
     (lts:rho:cost:event
       1
       (0 0 0)
       ((lts:rho:cost:funding
          split
          (rho:cost:sig-mul recv-spend send-spend)))
       (rho:cost:sig-mul recv-spend send-spend)))
    (rho:cost:par
      (rho:cost:purse split rho:cost:stack-empty)
      (rho:cost:signed rho:nil split-done)))))

; Distinct initial participants and purses do not acquire a false edge merely
; because the traced machine emits one event before the other.
!(assertEqualToResult
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv left $left-message
            (rho:cost:signed rho:nil left-done))
          (rho:send left
            (rho:cost:signed rho:nil left-payload)))
        left-spend)
      (rho:cost:signed
        (rho:par
          (rho:recv right $right-message
            (rho:cost:signed rho:nil right-done))
          (rho:send right
            (rho:cost:signed rho:nil right-payload)))
        right-spend)
      (rho:cost:purse left
        (rho:cost:stack-cons left-spend rho:cost:stack-empty))
      (rho:cost:purse right
        (rho:cost:stack-cons right-spend rho:cost:stack-empty))))
  ((lts:rho:cost:receipt
    ((lts:rho:cost:event
       0
       ()
       ((lts:rho:cost:funding left left-spend))
       left-spend)
     (lts:rho:cost:event
       1
       ()
       ((lts:rho:cost:funding right right-spend))
       right-spend))
    (rho:cost:par
      (rho:cost:purse left rho:cost:stack-empty)
      (rho:cost:purse right rho:cost:stack-empty)
      (rho:cost:signed rho:nil right-done)
      (rho:cost:signed rho:nil left-done)))))

; Equal raw spends are separate firing occurrences and are never deduplicated.
!(assertEqualToResult
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv left-equal $left-message
            (rho:cost:signed rho:nil left-equal-done))
          (rho:send left-equal
            (rho:cost:signed rho:nil left-equal-payload)))
        equal-spend)
      (rho:cost:signed
        (rho:par
          (rho:recv right-equal $right-message
            (rho:cost:signed rho:nil right-equal-done))
          (rho:send right-equal
            (rho:cost:signed rho:nil right-equal-payload)))
        equal-spend)
      (rho:cost:purse left-equal
        (rho:cost:stack-cons equal-spend rho:cost:stack-empty))
      (rho:cost:purse right-equal
        (rho:cost:stack-cons equal-spend rho:cost:stack-empty))))
  ((lts:rho:cost:receipt
    ((lts:rho:cost:event
       0
       ()
       ((lts:rho:cost:funding left-equal equal-spend))
       equal-spend)
     (lts:rho:cost:event
       1
       ()
       ((lts:rho:cost:funding right-equal equal-spend))
       equal-spend))
    (rho:cost:par
      (rho:cost:purse left-equal rho:cost:stack-empty)
      (rho:cost:purse right-equal rho:cost:stack-empty)
      (rho:cost:signed rho:nil left-equal-done)
      (rho:cost:signed rho:nil right-equal-done)))))

!(assertEqualToResult
  (lts:rho:cost:causal-trace rho:cost:nil)
  ((lts:rho:cost:receipt () rho:cost:nil)))

; Causal records are output-only in this tranche. The only input boundary is
; the cost-term parser, which rejects a non-cost term explicitly.
!(assertEqualToResult
  (lts:rho:cost:causal-trace rho:poop)
  ((Error
     (lts:rho:cost:causal-trace rho:poop)
     "unsupported cost-rho term")))

; Zero firing fuel performs no cover search and reports only that the firing
; allowance ended.
!(assertEqualToResult
  (lts:rho:cost:causal-prefix 0 100
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv bounded $message
            (rho:cost:signed rho:nil bounded-done))
          (rho:send bounded
            (rho:cost:signed rho:nil bounded-payload)))
        bounded-spend)
      (rho:cost:purse bounded
        (rho:cost:stack-cons bounded-spend rho:cost:stack-empty))))
  ((lts:rho:cost:prefix
    lts:rho:cost:fuel-exhausted
    (lts:rho:cost:receipt
      ()
      (rho:cost:par
        (rho:cost:purse bounded
          (rho:cost:stack-cons bounded-spend rho:cost:stack-empty))
        (rho:cost:signed
          (rho:par
            (rho:recv bounded $message
              (rho:cost:signed rho:nil bounded-done))
            (rho:send bounded
              (rho:cost:signed rho:nil bounded-payload)))
          bounded-spend))))))

; Reaching the firing bound stops before another frontier check.
!(assertEqualToResult
  (lts:rho:cost:causal-prefix 1 100
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv bounded $message
            (rho:cost:signed rho:nil bounded-done))
          (rho:send bounded
            (rho:cost:signed rho:nil bounded-payload)))
        bounded-spend)
      (rho:cost:purse bounded
        (rho:cost:stack-cons bounded-spend rho:cost:stack-empty))))
  ((lts:rho:cost:prefix
    lts:rho:cost:fuel-exhausted
    (lts:rho:cost:receipt
      ((lts:rho:cost:event
        0
        ()
        ((lts:rho:cost:funding bounded bounded-spend))
        bounded-spend))
      (rho:cost:par
        (rho:cost:purse bounded rho:cost:stack-empty)
        (rho:cost:signed rho:nil bounded-done))))))

; With zero firing fuel even a normal form is not inspected.
!(assertEqualToResult
  (lts:rho:cost:causal-prefix 0 100 rho:cost:nil)
  ((lts:rho:cost:prefix
    lts:rho:cost:fuel-exhausted
    (lts:rho:cost:receipt () rho:cost:nil))))

; Search exhaustion is distinct from both quiescence and firing exhaustion.
!(assertEqualToResult
  (lts:rho:cost:causal-prefix 1 1
    (rho:cost:signed
      (rho:par
        (rho:recv bounded $message
          (rho:cost:signed rho:nil bounded-done))
        (rho:send bounded
          (rho:cost:signed rho:nil bounded-payload)))
      bounded-spend))
  ((lts:rho:cost:prefix
    lts:rho:cost:search-exhausted
    (lts:rho:cost:receipt
      ()
      (rho:cost:signed
        (rho:par
          (rho:recv bounded $message
            (rho:cost:signed rho:nil bounded-done))
          (rho:send bounded
            (rho:cost:signed rho:nil bounded-payload)))
        bounded-spend)))))

; Bounds are nonnegative machine integers.
!(assertEqualToResult
  (lts:rho:cost:causal-prefix -1 0 rho:cost:nil)
  ((Error
    (lts:rho:cost:causal-prefix -1 0 rho:cost:nil)
    "expected a nonnegative integer fuel bound")))

!(assertEqualToResult
  (lts:rho:cost:causal-prefix 0 -1 rho:cost:nil)
  ((Error
    (lts:rho:cost:causal-prefix 0 -1 rho:cost:nil)
    "expected a nonnegative integer search bound")))

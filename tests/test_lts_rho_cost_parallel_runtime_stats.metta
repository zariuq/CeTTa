!(import! &self lts:rho:cost)

; Two disjoint funded reactions must inhabit one concurrent wave. Their receipt
; keeps both occurrences and introduces no causal edge between them.
!(reset-runtime-stats!)
!(pragma! num-threads 4)
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
       0 () ((lts:rho:cost:funding left left-spend)) left-spend)
     (lts:rho:cost:event
       1 () ((lts:rho:cost:funding right right-spend)) right-spend))
    (rho:cost:par
      (rho:cost:purse left rho:cost:stack-empty)
      (rho:cost:purse right rho:cost:stack-empty)
      (rho:cost:signed rho:nil right-done)
      (rho:cost:signed rho:nil left-done)))))
!(bind! &independent-stats (runtime-stats!))
!(assertEqualToResult
  (match &independent-stats
    (runtime-counter cost-rho-parallel-wave-width-peak $n)
    (== $n 2))
  (True))
!(assertEqualToResult
  (match &independent-stats
    (runtime-counter cost-rho-parallel-wave $n)
    (== $n 1))
  (True))
!(assertEqualToResult
  (match &independent-stats
    (runtime-counter cost-rho-parallel-firing $n)
    (== $n 2))
  (True))

; A single purse occurrence is a contention point. Its tail is produced by the
; first withdrawal, so the second event is causally downstream in a later wave.
!(reset-runtime-stats!)
!(assertEqualToResult
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv shared $message-a
            (rho:cost:signed rho:nil done-a))
          (rho:send shared
            (rho:cost:signed rho:nil payload-a)))
        spend)
      (rho:cost:signed
        (rho:par
          (rho:recv shared $message-b
            (rho:cost:signed rho:nil done-b))
          (rho:send shared
            (rho:cost:signed rho:nil payload-b)))
        spend)
      (rho:cost:purse shared
        (rho:cost:stack-cons spend
          (rho:cost:stack-cons spend rho:cost:stack-empty)))))
  ((lts:rho:cost:receipt
    ((lts:rho:cost:event
       0 () ((lts:rho:cost:funding shared spend)) spend)
     (lts:rho:cost:event
       1 (0) ((lts:rho:cost:funding shared spend)) spend))
    (rho:cost:par
      (rho:cost:purse shared rho:cost:stack-empty)
      (rho:cost:signed rho:nil done-a)
      (rho:cost:signed rho:nil done-b)))))
!(bind! &contended-stats (runtime-stats!))
!(assertEqualToResult
  (match &contended-stats
    (runtime-counter cost-rho-parallel-wave-width-peak $n)
    (== $n 1))
  (True))
!(assertEqualToResult
  (match &contended-stats
    (runtime-counter cost-rho-parallel-wave $n)
    (== $n 2))
  (True))
!(assertEqualToResult
  (match &contended-stats
    (runtime-counter cost-rho-parallel-firing $n)
    (== $n 2))
  (True))

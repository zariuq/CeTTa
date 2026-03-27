; Miner-style regression: superpose should reduce nested expressions that
; interact with spaces and ordinary control flow.

!(let $s (new-space)
  (let $_ (add-atom $s (= marker a))
    (assertEqualToResult
      (superpose (
        (collapse (match $s (= marker $x) $x))
        (if True ok bad)))
      ((a) ok))))

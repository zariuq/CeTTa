; HE-extended fold-by-key: consume nondeterministic results into one
; accumulator per grouping key without rerunning the source query.

!(collapse
    (fold-by-key
      (superpose ((a 1) (b 2) (a 3) (c 4) (b 5)))
      0
      $acc
      $item
      (let ($key $value) $item $key)
      (let ($key $value) $item
        (eval (+ $acc $value)))))

!(collapse
    (fold-by-key
      (search-policy recursive-dependent-proof (order reverse))
      (superpose ((a 1) (b 2) (a 3) (c 4) (b 5)))
      0
      $acc
      $item
      (let ($key $value) $item $key)
      (let ($key $value) $item
        (eval (+ $acc $value)))))

!(assertEqualToResult
    (fold-by-key
      (superpose ())
      0
      $acc
      $item
      $item
      (eval (+ $acc 1)))
    ())

!(assertEqual
    (collapse
      (fold-by-key
        (superpose ((a 1) Empty (a 2) (b 3)))
        0
        $acc
        $item
        (let ($key $value) $item $key)
        (let ($key $value) $item
          (eval (+ $acc $value)))))
    ((a 3) (b 3)))

!(fold-by-key
    (superpose (1))
    0
    $acc
    $item
    (superpose (a b))
    (eval (+ $acc $item)))

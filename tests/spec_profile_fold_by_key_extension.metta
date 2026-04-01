!(collapse
    (fold-by-key
      (superpose ((a 1) (b 2) (a 3)))
      0
      $acc
      $item
      (let ($key $value) $item $key)
      (let ($key $value) $item
        (eval (+ $acc $value)))))

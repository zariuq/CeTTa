!(bind! &kb (new-space))
!(add-atoms &kb
  ((noise 0) (noise 1) (noise 2) (noise 3) (noise 4) (noise 5) (noise 6)
   (noise 7) (noise 8) (noise 9) (noise 10) (noise 11) (noise 12)))
!(add-atoms &kb
  ((:= (I $x) $x)
   (:= ((K $x) $y) $x)
   (:= (K $x $y) $x)
   (:= (S $x $y $z) ($x $z ($y $z)))))

!(assertEqualToResult
  (match &kb
    (:= (S K K x) $r)
    $r)
  ((K x (K x))))

!(add-atoms &kb
  ((:= (Add $x Z) $x)
   (:= (Add $x (S $y)) (Add (S $x) $y))))

!(assertEqualToResult
  (match &kb
    (:= (Add (S Z) (S Z)) $r)
    $r)
  ((Add (S (S Z)) Z)))


(pair a b)
(pair b c)

!(assertEqualToResult
  (match &self (pair a $x) $x)
  (b))
!(assertEqualToResult
  (match &self
    (, (pair $x $y)
       (pair $y $z))
    (path $x $y $z))
  ((path a b c)))

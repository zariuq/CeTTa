(= (prepend-one $head $tail)
   (append ($head) $tail))

!(let (prepend-one $head $tail)
      (a b c)
      ($head $tail))

!(let (prepend-one $head $tail)
      ()
      ($head $tail))

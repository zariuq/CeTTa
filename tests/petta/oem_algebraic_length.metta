(: spine-length (-> Atom %Undefined%))
(= (spine-length $xs)
   (case $xs
     ((End 0)
      ((Link $head $tail) (+ 1 (spine-length $tail))))))

; A near recurrence must retain ordinary execution: its successor is two.
(: spine-length-by-two (-> Atom %Undefined%))
(= (spine-length-by-two $xs)
   (case $xs
     ((End 0)
      ((Link $head $tail) (+ 2 (spine-length-by-two $tail))))))

!(spine-length (Link a (Link b (Link c End))))
!(spine-length-by-two (Link a (Link b (Link c End))))
!(spine-length Other)
!(once (spine-length $open))

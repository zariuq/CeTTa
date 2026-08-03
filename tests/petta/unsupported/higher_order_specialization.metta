(= (specialized $f)
   (if (= ($f 1) 2)
       (specialized (+ 2))
       ($f 1)))

!(specialized (+ 1))

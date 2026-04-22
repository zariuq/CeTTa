(= (wu) (case (empty)
              ((1 2)
               (Empty 42))))

(= (f) 42)

(= (wu2) (case (f)
               ((42 ok)
                (Empty nok))))

(= (g) 99)

!(assertEqual (wu) 42)
!(assertEqual (wu2) ok)
!(assertEqualToResult (case (g)
                            ((1 one)
                             (2 two)
                             (Empty fallback)))
                      ())

(= (returned-add) (+))

!(test (returned-add 1 1) 2)

(= (compose $left $right $argument)
   ($left ($right $argument)))

(= (plus-one-times-two)
   (compose (* 2) (+ 1)))

!(test (plus-one-times-two 1) 4)

; superpose should continue evaluation on each distributed alternative, not
; return the raw elements as data.

!(assertEqualToResult (superpose ((+ 1 2) (+ 3 4))) (3 7))
!(assertEqualToResult (superpose ((if True ok bad) (if False bad ok))) (ok ok))

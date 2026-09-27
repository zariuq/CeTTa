; An Error is a value.  A query whose answer is an Error reports it as its
; answer, and the run goes on with the next query.
!(Error foo bar)
!(+ 1 2)
(= (checked-div $a $b) (if (== $b 0) (Error (checked-div $a $b) DivisionByZero) (/ $a $b)))
!(checked-div 6 0)
!(checked-div 6 3)
!(superpose ((Error one first) two))
!(+ 2 3)

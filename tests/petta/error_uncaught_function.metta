; An error in the body of a function called directly propagates to the run.
(= (f $x) (/ $x 0))
!(f 5)
!(println! after)

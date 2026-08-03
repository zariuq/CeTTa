(= (successor b a) True)
(= (successor c b) True)
(= (successor d c) True)

(= (later $x $y)
   (successor $x $y))
(= (later $x $y)
   (and (successor $x $middle)
        (later $middle $y)))

!(collapse ((later d $answer) $answer))

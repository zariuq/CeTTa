; Minimal recursive logic-programming witness distilled from logicprog.metta.

(= (successor b a)
   True)
(= (successor c b)
   True)
(= (successor d c)
   True)

(= (later-in-alphabet $X $Y)
   (successor $X $Y))

(= (later-in-alphabet $X $Y)
   (and (successor $X $Z)
        (later-in-alphabet $Z $Y)))

!(test (collapse ((later-in-alphabet d $1) $1))
       ((True c) (True b) (True a)))

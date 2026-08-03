; Length is the forward list-size relation used by PeTTa programs.
!(test (length (a b c)) 3)

; The empty expression is the empty Prolog list.
!(test (length ()) 0)

; The argument is evaluated, preserving one result per nondeterministic list.
!(test
    (collapse
      (length
        (superpose ((a) (a b)))))
    (1 2))

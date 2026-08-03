; Parsed atoms are returned directly.
!(test (parse "A") A)

; Parsed expressions remain syntax data.
!(test (parse "(R A (S B C))") (R A (S B C)))

; In particular, a parsed arithmetic expression is not executed.
!(test (parse "(* 2 21)") (quote (* 2 21)))

; Quoted text round-trips as a string atom.
!(test (parse "\"42\"") "42")

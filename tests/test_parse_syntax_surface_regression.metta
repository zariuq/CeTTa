!(assertEqual (parse "A") A)
!(assertEqual (parse "(R A B)") (R A B))
!(assertEqual (parse "(* 2 21)") (quote (* 2 21)))
!(parse "(R")

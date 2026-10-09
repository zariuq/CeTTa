!(bind! &child-token 7)
(= (child-parses-local) (parse "&child-token"))
(= (child-reads-local) &child-token)
(= (child-parses-top) (parse "&top-token"))

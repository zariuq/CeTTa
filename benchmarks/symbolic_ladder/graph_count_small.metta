(edge a b)
(edge b c)
(edge b d)
!(foldall + (match &self (, (edge $x $y) (edge $y $z)) 1) 0)

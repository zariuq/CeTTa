(= (g $X $Y $Z)
   (append ((#+ $X $Z)) $Y))

!(let (g $X $Y 35) (42 2 3)
   ($X $Y 40))

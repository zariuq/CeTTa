(= (f $X $Y)
   (append ($X) $Y))

(= (g $X $Y $Z)
   (append ((#+ $X $Z)) $Y))

!(let (f $Head $Tail) (1 2 3 4 5 6)
   ($Head $Tail))

!(let (g $X $Y 35) (42 2 3)
   ($X $Y 40))

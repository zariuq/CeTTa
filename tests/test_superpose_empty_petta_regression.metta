; Petta treats `superpose` as a branch packet: empty branches disappear and
; surviving siblings still flow through.

!(assertEqual (superpose ((empty) full))
              full)

(= (wu1)
   (empty))

(= (wu2)
   (full))

(= (wu)
   (superpose ((wu1)
               (wu2))))

!(assertEqual (wu)
              (full))

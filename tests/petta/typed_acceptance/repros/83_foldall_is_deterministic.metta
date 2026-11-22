(: sum-generated (-[det]-> Number))
(= (sum-generated)
   (foldall + (superpose (1 2 3)) 0))

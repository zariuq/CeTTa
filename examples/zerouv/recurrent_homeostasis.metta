(= depleted replenishing)
(= replenishing balanced)
(= balanced depleted)

(= (homeostat depleted) replenishing)
(= (homeostat replenishing) balanced)
(= (homeostat balanced) depleted)

(accept homeostat balanced)

(! (recur homeostat
    (path depleted
      (path replenishing
        (path balanced
          (path depleted end))))
    balanced))

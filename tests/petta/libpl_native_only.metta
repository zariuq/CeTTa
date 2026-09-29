; min and max are native: a program whose only stdlib names are theirs
; answers without starting the Prolog engine.
(= (clamp $v $lo $hi) (min $hi (max $v $lo)))

!(min 1.5 2.5)
!(max 3 4.5)
!(min 2 1)
!(clamp 5 0 3)
!(clamp -1.5 0 3)
!(clamp 1.25 0 3)

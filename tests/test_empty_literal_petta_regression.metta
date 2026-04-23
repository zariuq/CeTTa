(= (f Empty) 1)
(= (wrap $x) (quote $x))

!(assertEqual (f Empty) 1)
!(assertEqual (collapse (f 7)) ())
!(assertEqual (wrap Empty) (quote Empty))
!(assertEqual (wrap 7) (quote 7))

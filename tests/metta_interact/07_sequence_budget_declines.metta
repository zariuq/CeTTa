(site r1 inspect (task $x) (checked $x) (s z))
(site r1 solve (checked $x) (answer $x) (s (s z)))
(! (run r1 (s (s z)) (task alpha)
    (then inspect (then solve end))))

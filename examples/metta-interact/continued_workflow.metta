(site model-v1 inspect (task $x) (checked $x) (s z))
(site model-v1 solve (checked $x) (answer $x) (s (s z)))

(! (run model-v1 (s (s (s z))) (task alpha)
    (then inspect (then solve end))))

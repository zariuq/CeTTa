(: at A)
(: at T)
(: t T)

(: testf (-> $a $a))
(= (testf at) t)

!(test (testf at) t)
!(test (collapse (testf at)) (t))

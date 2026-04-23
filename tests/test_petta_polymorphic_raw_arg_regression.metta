(: mid (-> $a $a))
(= (mid $x) (let (a b) $x $x))

!(test (mid ($a b)) (a b))

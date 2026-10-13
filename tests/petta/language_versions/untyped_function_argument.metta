; An untyped function callable at an arrow parameter's arity satisfies it.
(: twice (-> (-> $t $t) $t $t))
(= (twice $f $x) ($f ($f $x)))
(= (succ $n) (+ $n 1))

!(test (twice succ 3) 5)
!(test (twice not False) False)

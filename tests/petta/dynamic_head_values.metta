;; A call whose head is a variable applies the variable's value, or keeps the
;; application as data when that value cannot be applied.

(= (succ $n) (+ $n 1))
(= (add $a $b) (+ $a $b))
(= (app $f $x) ($f $x))
(= (app2 $f $x $y) ($f $x $y))

(item (⊙ 2))
(item (succ 1))
(item (partial add (1)))

;; Values that cannot be applied: the application is data.
!(once (app (⊙ 2) 1))
!(once (app 3 1))
!(once (app "s" 1))
!(once (app foo 1))
!(let $e (match &self (item (succ 1)) (succ 1)) (once (app $e 5)))
!(collapse (let $e (superpose ((⊙ 2) 7 bar)) (app $e 1)))

;; Values that can be applied: the application is a call.
!(once (app succ 1))
!(once (app (add 1) 2))
!(once (app (|-> ($y) (* $y 10)) 4))
!(once (app2 add 2 3))

;; An open argument: the call answers for every value of the head.
!(collapse (let $f (superpose (succ (⊙ 1))) (app $f 1)))

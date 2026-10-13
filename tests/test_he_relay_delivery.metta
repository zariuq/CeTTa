; Answers reach their consumer through calls that only publish them.

(edge a b)
(edge b c)
(edge c d)
(edge b e)

(= (reach $x) (match &self (edge $x $y) $y))
(= (reach $x) (match &self (edge $x $y) (reach $y)))

; Ground answers through nested calls, in order, with repetition.
!(collapse (reach a))
!(unique (reach a))

; The answer is ground; the caller's bindings stay with it.
!(collapse (match &self (edge $x $y) (let $r (reach $y) ($x $r))))

; A ground answer below an open call: the call binds a caller variable.
(= (first-hop $x $y) (match &self (edge $x $y) done))
!(collapse (let $s (first-hop a $where) ($s $where)))

; Open answers keep their variables.
(= (open $k) (superpose ((pair $k $free) (pair $k 1))))
(= (wrap-open $k) (open $k))
!(collapse (let (pair $k $v) (wrap-open 7) ($k (get-metatype $v))))

; Empty beside answers is dropped; alone it stays empty.
(= (maybe $x) (if (== $x b) (empty) $x))
(= (relay-maybe $x) (maybe $x))
!(collapse (relay-maybe a))
!(collapse (relay-maybe b))
!(collapse (superpose ((relay-maybe a) (relay-maybe b) (relay-maybe c))))

; Errors beside answers are dropped; alone they are kept.
(= (risky $x) (if (== $x b) (Error $x bad) $x))
(= (relay-risky $x) (risky $x))
!(collapse (relay-risky b))
!(collapse (match &self (edge a $y) (relay-risky $y)))
!(collapse (superpose ((relay-risky a) (relay-risky b))))

; Deep recursion delivers each answer once.
(= (count-down $n) (if (== $n 0) (zero) (superpose ($n (count-down (- $n 1))))))
!(collapse (count-down 6))

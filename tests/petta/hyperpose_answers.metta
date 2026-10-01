; An answer of a hyperpose branch binds the caller's variables the branch
; binds, as SWI-PeTTa's concurrent_and copies each answer back and unifies
; it; a collection of the answers binds nothing outside it. The answers are
; SWI-PeTTa's, on one thread and on several.
!(let $v (hyperpose ((let $x 1 ok))) ($v $x))
!(collapse (let $v (hyperpose ((let $x 1 ok) (let $x 2 ok))) $x))
!(msort (collapse (let $v (hyperpose ((let $x 1 a) (let $y 2 b))) ($v $x $y))))
!(let $v (once (hyperpose ((let $x 1 ok)))) ($v $x))
!(collapse (hyperpose ((let $x 1 $x) (let $x 2 $x))))
!(let* (($v (collapse (hyperpose ((let $x 1 ok))))) ($x 7)) ($v $x))
!(msort (collapse (let $v (hyperpose ((let ($p $q) (a b) ok) (let ($p $q) (c d) ok))) ($p $q))))

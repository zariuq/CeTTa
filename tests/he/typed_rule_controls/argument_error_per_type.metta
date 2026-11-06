; HE tries each of an argument's types as its own branch: where none fits
; the parameter, each type reports its own error; where one fits, the call
; answers.
(: x A)
(: x B)
(: f (-> C C))
(= (f $v) $v)
(: g (-> B B))
(= (g $v) $v)
!(collapse (f x))
!(collapse (g x))
; A single type reports a single error.
(: y A)
!(collapse (f y))
; The same in a grounded operation and in the if condition.
!(collapse (+ x 1))
!(collapse (if x yes no))
(: OtherBox (-> $t $t BoxRange))
(: (OtherBox 1 2) OtherRange)
!(collapse (if (OtherBox 1 2) yes no))

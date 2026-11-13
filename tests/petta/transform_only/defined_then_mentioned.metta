; A body that defines a function and then mentions it: the mention was
; compiled before the definition, so in that invocation it is data.
(= (define-and-use)
   (let $u (add-atom &self (= (late $x) (+ $x 10))) (late 1)))
!(test (collapse (define-and-use)) ((late 1)))
!(test (late 1) 11)

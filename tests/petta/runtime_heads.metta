; A special form is syntax only where it is written.  Reached at run time --
; as the value of a variable head, as the head of the application a `reduce`
; dispatches, or as a callee passed to a function -- its arguments are
; evaluated first, and the application is data unless PeTTa also defines a
; function of that name, which is then called on their values.

(= (twice $x) (* 2 $x))
(= (app1 $f $a) ($f $a))
(= (app3 $f $a $b $c) ($f $a $b $c))
(= (run $a) (eval $a))
(= (dispatch $a) (reduce $a))
(= (safe $x) $x)

; A form without a function of its name is data.
!(let $f quote ($f (twice 4)))
!(let $f if ($f True 1 2))
!(let $f collapse ($f (twice 4)))
!(let $f case ($f 1 ((1 a) (2 b))))
!(reduce (progn 1 (twice 2)))
!(map-atom (1 2) quote)
!(app3 if True 1 2)

; `let` too: PeTTa names no function for it at any arity.
!(let $f let ($f $y 1 2))

; A form PeTTa also defines as a function runs on the values.
!(let $f eval ($f (quote (twice 4))))
!(reduce (eval (quote (twice 4))))
!(app1 eval (quote (twice 4)))
!(collapse (let $f superpose ($f (1 2))))
!(collapse (reduce (reduce (twice 2))))
!(collapse (app1 reduce (twice 4)))

; A held value is run, or dispatched, when its call runs.
!(run (quote (twice 4)))
!(dispatch (quote (twice 4)))
!(collapse (dispatch (quote (/ 1 0))))

; An ordinary head means the same written or reached.
!(let $f car-atom ($f (1 2)))
!(reduce (twice (+ 1 2)))

; An operation's own error inside a run-time dispatch fails that path.
!(collapse (reduce (/ 1 0)))
!(let $f / (collapse ($f 1 0)))
!(collapse (foldl-atom (1 0) 1 /))
!(catch (collapse (/ 1 0)))

; A callee that raises nothing is called directly once its caller is
; specialized; a raising equation added to it later is under the handler.
!(collapse (app1 safe 1))
!(add-atom &self (= (safe $x) (/ $x 0)))
!(msort (collapse (app1 safe 1)))

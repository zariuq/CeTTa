; An application (App f x) whose function is not a lambda does not reduce.
; It answers itself, as a call no equation answers does: an interpretation,
; not a value its producer completed.  Its argument is still the source it
; was written as.  A pattern that takes the argument apart passes it on as
; that source, so a call written inside it runs when a later demand reaches
; it, through an equation head, `let` or `case`, as on every other path.

(= (one-thunk) (Return (Const one)))

; A small call-by-push-value interpreter whose application is spelled App.
(= (lk (Cons $v $rest) $n) (if (== $n 0) $v (lk $rest (- $n 1))))
(= (vv $env (Var $n)) (lk $env $n))
(= (vv $env (Const $d)) (VConst $d))
(= (vv $env (Thunk $m)) (VClo $env $m))
(= (cc $env (Return $v)) (let $x (vv $env $v) (W (Ret $x))))
(= (cc $env (Force $v)) (let (VClo $e $m) (vv $env $v) (cc $e $m)))
(= (cc $env (Lam $n)) (W (LamClo $env $n)))
(= (cc $env (Bind $m $n)) (let (W $t) (cc $env $m) (then $env $t $n)))
(= (then $env (Ret $x) $n) (cc (Cons $x $env) $n))
(= (cc $env (App $m $v)) (let (W $t) (cc $env $m) (let $a (vv $env $v) (ap $t $a))))
(= (ap (LamClo $e $n) $a) (cc (Cons $a $e) $n))

; The same pending call reaches the interpreter through a function argument
; that is forced, through one that is returned and printed, through Bind,
; and directly: all four run it.
!(cc Nil (App (Lam (Force (Var 0))) (Thunk (one-thunk))))
!(cc Nil (App (Lam (Return (Var 0))) (Thunk (one-thunk))))
!(cc Nil (Bind (Return (Thunk (one-thunk))) (Force (Var 0))))
!(cc Nil (Force (Thunk (one-thunk))))

; The argument taken out of a stuck application by an equation head, by let
; and by case.
(= (argument-of (App $m $v)) (W $v))
!(argument-of (App x (Thunk (one-thunk))))
!(let (App $m $v) (App x (Thunk (one-thunk))) (W $v))
!(case (App x (Thunk (one-thunk))) (((App $m $v) (W $v))))

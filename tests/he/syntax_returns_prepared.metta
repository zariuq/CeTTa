; A definition whose range is Atom answers syntax, which HE evaluates only
; where a callee whose range is not Atom answers it.  Rust HE's switch and
; quote are such definitions; written here under other names, so that
; CeTTa's native forms do not answer them.
(: my-switch (-> %Undefined% Expression %Undefined%))
(= (my-switch $atom $cases)
   (id (my-switch-minimal $atom $cases)))
(: my-switch-minimal (-> Atom Expression Atom))
(= (my-switch-minimal $atom $cases)
  (function (chain (decons-atom $cases) $list
    (chain (eval (my-switch-internal $atom $list)) $res
      (chain (eval (if-equal $res NotReducible Empty $res)) $x (return $x)) ))))
(: my-switch-internal (-> Atom Expression Atom))
(= (my-switch-internal $atom (($pattern $template) $tail))
  (function (unify $atom $pattern
    (return $template)
    (chain (eval (my-switch-minimal $atom $tail)) $ret (return $ret)) )))
(: my-quote (-> Atom Atom))
(= (my-quote $atom) NotReducible)
(: q (-> Atom Atom))
(= (q $x) $x)

; A case's template is evaluated where switch answers it.
(= (classify $n) (my-switch $n ((0 zero) (1 (+ 1 1)) ($_ (* $n 10)))))
!(classify 0)
!(classify 1)
!(classify 7)
(= (sum-classes $n $acc)
   (if (== $n 0) $acc
       (sum-classes (- $n 1) (+ $acc (my-switch (- $n 1) ((0 1) (1 2) ($_ 3)))))))
!(sum-classes 50 0)
; No case matches: switch answers nothing.
!(collapse (my-switch 5 ((0 a) (1 b))))

; The syntax an Atom-range call answers is final; id, whose range is not
; Atom, evaluates the syntax it is given.
(= (held) (q (+ 1 2)))
(= (evaluated) (id (q (+ 1 2))))
!(held)
!(evaluated)
!(+ (held) 1)

; With range Atom, a function body runs and its return is the answer; any
; other body is answered as written, and runs only where it is evaluated.
(: q2 (-> Atom Atom))
(= (q2 $x) (chain (eval (+ 1 2)) $y ($y $x)))
(: q3 (-> Atom Atom))
(= (q3 $x) (function (return ($x (+ 1 2)))))
(: q4 (-> Atom Atom))
(= (q4 $x) (unify $x (a $z) $z none))
!(let $r (q2 a) (car-atom $r))
!(q3 a)
!(let $r (q4 (a b)) (car-atom $r))
!(id (q2 a))
!(id (q3 a))
!(id (q4 (a (+ 1 2))))

; quote answers its own call.
(= (quoted $x) (my-quote (pair $x)))
!(quoted 3)
(= (count-quotes $n $acc)
   (if (== $n 0) $acc (let $q (my-quote (a b)) (count-quotes (- $n 1) (+ $acc 1)))))
!(count-quotes 100 0)

; A type variable written once accepts every value and asks nothing of the
; answer.  One that is a parameter's and the range asks the answer to have
; the argument's type, which a definition answering that argument has.
(: same (-> $t $t))
(= (same $x) $x)
(= (twice-same $n) (+ (same $n) (same $n)))
!(twice-same 4)
(: pick (-> $t $u $t))
(= (pick $x $y) $x)
(= (pick-sum $n) (+ (pick $n "b") 1))
!(pick-sum 4)
(: tag (-> Number $r))
(= (tag $x) "s")
(= (tagged $n) (tag (+ $n 1)))
!(tagged 4)
; Another answer has a type of its own.  Given the syntax an Atom-range call
; answers, the argument's type asks nothing.  The reference runner stops the
; document on the error, so it comes last.
(: same-wrong (-> $t $t))
(= (same-wrong $x) "wrong")
!(same-wrong (q (+ 1 2)))
!(same-wrong 4)

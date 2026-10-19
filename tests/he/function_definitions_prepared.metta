; Definitions written as HE functions (function, chain, eval, unify, return)
; run in the prepared machine as HE's instruction machine runs them: a chain
; over one evaluation step of a function continues in that function's
; returns, so each arm's continuation sees its own template, and a case with
; no arm that matches, or whose arm is NotReducible, answers nothing.
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
(: arms (-> %Undefined% Expression %Undefined%))
(= (arms $atom $cases)
  (chain (decons-atom $cases) $ht
    (unify ($first $rest) $ht
      (unify ($pattern $template) $first
        (unify $atom $pattern $template (arms $atom $rest))
        (Error (arms $atom $cases) "pairs expected"))
      Empty)))
(: twice (-> %Undefined% %Undefined%))
(= (twice $x) (function (chain (eval (+ 0 1)) $one (return (+ $x $x)))))
(= (classify $n) (arms (- $n 1) ((0 zero) (1 one) ((S $k) (succ $k)) ($_ many))))
(= (count $n $acc) (if (== $n 0) $acc (count (- $n 1) (+ $acc (arms (% $n 3) ((0 1) (1 10) ($_ 100)))))))
!(my-switch-minimal 5 ((4 False) (5 (+ 1 2))))
!(my-switch-minimal (a 7) (((a $x) (+ $x 1)) ($_ other)))
!(collapse (my-switch-minimal 6 ((4 False) (5 (+ 1 2)))))
!(collapse (my-switch-minimal 5 ((5 NotReducible) ($_ other))))
!(classify 1)
!(classify 2)
!(classify 9)
!(arms (S 4) (((S $k) (+ $k 1)) ($_ none)))
!(collapse (arms 3 ((1 a) (2 b))))
!(count 30 0)
; Macro ranges and parameters: an Atom range answers the body as written, an
; Expression range is evaluated as %Undefined% is, and a variable given to an
; Expression parameter must hold an expression.
(: r-expr (-> Expression Expression))
(= (r-expr $x) $x)
(: r-atom (-> Expression Atom))
(= (r-atom $x) $x)
(= (wrap $l) (r-expr $l))
(= (wrap-atom $l) (r-atom $l))
!(r-expr ((+ 1 1)))
!(r-atom ((+ 1 1)))
!(wrap (7 8))
!(wrap-atom ((+ 2 2)))
; HE's unify matches its operands with each other, whichever holds the value.
(: my-car (-> Expression %Undefined%))
(= (my-car $atom)
  (chain (decons-atom $atom) $ht (unify ($head $_) $ht
    $head
    (Error (my-car $atom) "car-atom expects a non-empty expression as an argument") )))
(= (first-of $l) (my-car $l))
(= (sum-firsts $n $acc) (if (== $n 0) $acc (sum-firsts (- $n 1) (+ $acc (first-of ($n 0))))))
!(first-of (7 8 9))
!(sum-firsts 50 0)
; What a binding carries out of a matched operand is evaluated where it goes.
(= (g) 5)
(= (u) (unify (g) $x $x none))
(fact 1)
(= (um) (unify (match &self (fact $y) $y) $x $x none))
!(u)
!(um)
!(collapse (wrap 5))

; He-compat raw eval of a three-argument let publishes the Rust stdlib step
; (unify <value> <pattern> <template> Empty) beside any let equation already
; in the space. The caller interprets that step. Wrong arity and a missing
; name stay the NotReducible protocol. The let equation is not stored.
!(eval (let $a 7 $a))
!(eval (let $a (+ 1 2) $a))
!(eval (let $a 7))
!(eval (no-such))
!(match &self (= (let $pattern $atom $template) $rhs) $rhs)
(= (ground)
   (function (chain (let $a 7 $a) $s (chain (eval $s) $t (return $t)))))
!(ground)
(= (changed $n)
   (function (chain (let $a $n $a) $s (chain (eval $s) $t (return $t)))))
!(changed 7)
!(changed 8)
(= (effect)
   (function (chain (let $a 7 (println! 9)) $s (chain (eval $s) $t (return $t)))))
!(effect)
(= (bound-env)
   (function
     (chain (let $a 7 (unify $a $z (format-args "{}|{}" ($a $z)) Fail)) $selected
       (chain (eval $selected) $text (return $text)))))
!(bound-env)
(= (let $p $a $t) (user-let $a))
!(eval (let $x 1 $x))
!(eval (+ 1 2))
(= (nr) (function (return NotReducible)))
!(nr)

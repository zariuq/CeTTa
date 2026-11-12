; Call-return boundaries of ordinary and raw invocation. The expected
; output agrees with Hyperon Experimental 0.2.10.

; A ground equation whose right-hand side is NotReducible declines the call.
(= (freeze 7) NotReducible)
!(freeze 7)

; A returned payload is data: it is not evaluated.
!(function (return (println! 9)))

; Raw chain over a selected unify branch.
!(eval (chain (unify A A B C) $slot (cons-atom $slot (Tail))))

; Ordinary unify evaluates its selected branch; an Atom demand holds it.
(= (value-of) Value)
(: hold (-> Atom Atom))
(= (hold $x) $x)
!(unify X Y Fail (value-of))
!(hold (unify X X (value-of) Fail))

; Duplicate function-valued equations give duplicate occurrences.
(= (supply $x) (function (return $x)))
(= (supply $x) (function (return $x)))
!(collapse (eval (function (chain (eval (supply Value)) $r (return (Pair $r $r))))))
!(eval (function (chain (unify Value $x (Pair $x $x) Fail) $r (return $r))))
!(eval (function (chain (eval (missing)) $r (return (Seen $r)))))
!(eval (function (chain (unify A B Yes Empty) $r (return (Seen $r)))))

(: producer (-> Atom Symbol))
(= (producer $x) (function (return $x)))
(= (producer $x) (function (return $x)))
!(collapse (producer Value))
!(collapse (eval (producer Value)))

; Nested aliases are resolved through their equality classes.
!(let $u $w (let $w $v (unify $u $v Yes No)))
!(unify $u $w (unify $w $v (unify $u $v Yes No) No) No)
!(unify A B Yes No)

; A named function block returns its chain value; eval of a selected branch
; runs it once.
(= (return-let) (function (chain (let $a 7 $a) $s (return $s))))
(= (return-let-effect) (function (chain (let $a 7 (println! 9)) $s (return $s))))
(= (return-unify-effect) (function (chain (unify 7 7 (println! 9) Empty) $s (return $s))))
(= (eval-unify-effect)
   (function (chain (unify 7 7 (println! 9) Empty) $s (chain (eval $s) $t (return $t)))))
(= (return-effect) (function (return (println! 9))))
(= (return-seven) (function (return 7)))
!(return-let)
!(return-let-effect)
!(return-unify-effect)
!(eval-unify-effect)
!(return-effect)
!(return-seven)

; Independent caller variables stay independent through an equation.
(= (pair-of $p $q) (pair $p $q))
!(eval (pair-of $a $b))

; A three-argument unify is malformed. Its error ends the run, so it is last.
!(unify $a A B)

; Quotation suspends evaluation, not binding.  Every binder form reaches the
; variables below quote with the same value: lexical let and let*, the
; matcher-driven chain, unify and match, and equation rewriting.  What the
; quote keeps is the syntax around them, unevaluated.

!(let $x a (quote $x))
!(let* (($x a)) (quote $x))
!(chain a $x (quote $x))
!(unify $x a (quote $x) no)
!(let $s (new-space)
   (let $_ (add-atom $s (P a))
     (match $s (P $x) (quote $x))))
(= (quote-named $x) (quote $x))
!(quote-named a)

; The substituted value sits inside syntax that is still not evaluated.
!(let $x 2 (quote (+ 1 $x)))
!(chain 2 $x (quote (+ 1 $x)))

; Lexical activations retain captured code and authored relation results.
(scope:profile lexical-fresh)
!(bind! &kb (new-space))
!(add-atom &kb (fact @a True))
(= (holds $statement)
   (match &kb (fact $statement $truth) $truth){$statement})
!(match &kb (fact @a $truth) $truth)
!(holds @a)
!(collapse (holds @b))
; Resolving a delayed pattern field must not execute other pattern syntax.
(= (tick) (println! should-not-run))
!(add-atom &kb (syntax (tick) @a))
(= (inspect $code) (match &kb (syntax (tick) $code) found){$code})
!(inspect @a)
!(collapse (inspect @b))
; An authored Boolean relation takes precedence over dialect intrinsics.
(= (member $x $xs)
   (if (== $xs ()) False
       (let ($h $t) (decons-atom $xs)
         (if (== $h $x) True (member $x $t)))))
!(let $first (member A (A)) (let $second (member A (B)) (Pair $first $second)))
; A call with no matching equation remains data; a matched failing body fails.
(= (value (Box $x)) $x)
(= (failure a) (empty))
!(let $v (value 1) (Pair $v (value (Box 2))))
!(collapse (failure a))
!(failure b)

; Computing the whole pattern is distinct from executing its syntax fields.
!(bind! &quoted (new-space))
!(add-atom &quoted @(fact a))
!(match &quoted (lift app @fact @a) produced)
!(collapse (match &quoted (lift app @fact @b) produced))

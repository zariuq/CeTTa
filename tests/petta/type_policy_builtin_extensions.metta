; Static primitive predicates are protected only at their own arity.
(= (max $x) (UnaryMax $x))
!(max a)
!(collapse (let (Error $formal $context)
    (catch (add-atom &self (= (max $a $b) (UserMax $a $b)))) rejected))
!(collapse (match &self (= (max $a $b) $body) stored-max))
!(collapse (max 2 3))
!(collapse (let (Error $formal $context)
    (catch (add-atom &self (= (min $a $b) (UserMin $a $b)))) rejected))
!(collapse (match &self (= (min $a $b) $body) stored-min))
!(collapse (min 2 3))

; A harmless sequence and binding preserve all relation alternatives.
(= (policy-nested $unused)
   (progn ignored (let $v (max 2 3) (Seen $v))))
!(collapse (policy-nested unit))

!(collapse (let (Error $formal $context)
    (catch (add-atom &self (= (chain $x) (UnaryChain $x)))) rejected))
!(collapse (match &self (= (chain $x) $body) stored-chain-one))

; The chain form and an equation of the same arity compose in extended.
(= (chain $value $binder $body) (UserChain $value))
!(collapse (chain (+ 1 2) $value (BuiltinChain $value)))

; A separate evaluation owner has its own authored equations.
!(bind! &policy-kb (new-space))
!(add-atom &policy-kb (= (min $a $b) (OwnedMin $a $b)))
!(collapse (eval (min 2 3) &policy-kb))

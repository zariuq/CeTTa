; Each branch retains its own argument demand. The primitive evaluates;
; the authored Atom domains receive source syntax and hold their result.
(: max (-> Atom Atom Atom))
(= (max $a $b) (RawMax $a $b))
(= (policy-operand) (progn (println! operand) 3))
(= (policy-raw-caller $unused)
   (progn ignored (max (policy-operand) 4)))
!(collapse (policy-raw-caller unit))

; A failing authored result guard cannot erase the primitive's answer.
(: min (-> Number Number Number))
(= (min $a $b) rejected)
(= (policy-guarded-caller $unused) (min (+ 1 1) 3))
!(collapse (policy-guarded-caller unit))

; Source reduction, dynamic heads and completed partials retain alternatives.
!(collapse (reduce (max 2 4)))
!(collapse (let $f max ($f 2 4)))
!(collapse (let $value (+ 1 2)
    (let $partial (max $value) ($partial 4))))
; Atom-domain partial capture remains raw; completing it must not run that code.
!(collapse (let (Error $formal $context)
    (catch (let $partial (max (+ 1 2)) ($partial 4))) raw-partial-fault))

; The demanded output must not prune the mutation that enables that answer.
(= (policy-late-chain)
   (progn
      (add-atom &self (: chain (-> Atom Atom Atom Atom)))
      (add-atom &self (= (chain $a $b $c) extension-value))
      (chain (quote 1) $value (quote builtin-value))))
!(collapse (let extension-value (policy-late-chain) matched))
!(collapse (chain (quote 1) $value (quote builtin-value)))

; Primitive operands can themselves have builtin and authored answers.
(= (+ $a $b) 7)
!(collapse (max (+ 1 2) 4))

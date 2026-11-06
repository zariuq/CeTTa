; A space that authors equations for a builtin head evaluates it by them,
; nested in a body as at the root; in &self the builtin is protected.
!(bind! &kb (new-space))
!(add-atom &kb (= (+ $a $b) (OwnedPlus $a $b)))
!(add-atom &kb (= (and $a $b) (OwnedAnd $a $b)))
!(add-atom &kb (= (min $a $b) (OwnedMin $a $b)))
!(add-atom &kb (= (fp $x) (+ $x 1)))
!(add-atom &kb (= (fa $x) (and $x true)))
!(add-atom &kb (= (fm $x) (min $x 5)))
!(collapse (eval (fp 2) &kb))
!(collapse (eval (fa true) &kb))
!(collapse (eval (fm 2) &kb))
(= (gm $x) (min $x 5))
!(gm 2)

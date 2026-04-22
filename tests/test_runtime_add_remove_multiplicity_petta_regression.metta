; Runtime add/remove should preserve multiplicity for adapted Petta declarations.

!(test
  (let* (($a1 (add-atom &self (= (dupfun) 42)))
         ($a2 (add-atom &self (= (dupfun) 42)))
         ($before (collapse (match &self (= (dupfun) $x) $x)))
         ($_ (remove-atom &self (= (dupfun) 42)))
         ($after (collapse (match &self (= (dupfun) $x) $x)))
         ($value (collapse (dupfun))))
    ($before $after $value))
  ((42 42) (42) (42)))

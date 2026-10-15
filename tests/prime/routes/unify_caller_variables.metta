; Matching fresh local pattern variables must retain the caller's code
; variable representatives, including through nested typed abstractions.
!(import! &self ../../../examples/prime/nupln_lambda/nupln_lambda_core.metta)
!(bind! &kb (new-space))
!(add-atom &kb (nu:domain Person))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (nu:bias ∨))
!(add-atom &kb (nu:bias exists))
!(add-atom &kb (nu:bias fix))
!(add-atom &kb (: parent (-> Person (-> Person Bool))))
!(let (MkSized $n (: $term $type))
   (nu:type &kb nil 10
     (: @(lam $x (lam $z (parent $x $z))) (-> Person (-> Person Bool))))
   (== $term @(lam $a (lam $b (parent $a $b)))))
!(let (MkSized $n (: $term $type))
   (nu:type &kb nil 10
     (: @(lam $x (lam $z (parent $z $x))) (-> Person (-> Person Bool))))
   (== $term @(lam $a (lam $b (parent $b $a)))))
!(let (MkSized $n (: $term $type))
   (select (nu:type &kb nil 40
     (: @(fix (lam $self (lam $x (lam $z
          (∨ (parent $x $z)
             (exists Person (lam $y (∧ (parent $x $y) ($self $y $z)))))))))
        (-> Person (-> Person Bool)))))
   (== $term @(fix (lam $recur (lam $a (lam $b
          (∨ (parent $a $b)
             (exists Person (lam $c (∧ (parent $a $c) ($recur $c $b)))))))))))

(hol:profile base)
!(import! &self judgments.metta)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &math add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; This record is unsealed data. A consumer rechecks exact endpoints in its
; selected scope; an arbitrary identity proof cannot replace type:eq.
(= (conversion-recheck $scope (quote (Conversion $source $target $type $proof)))
   (and (type:check $scope $source $type)
     (and (type:check $scope $target $type)
       (and (type:eq $scope $source $target)
            (type:check $scope $proof (id $type $source $target))))))
(= (conversion-proposal $scope (quote $term))
   (let $normalized (type:normalize $scope $term)
     (case $normalized
       (((TypeNormalForm $source $target $source-type $target-type)
         (quote (Conversion $source $target $target-type (refl $source))){$source $target $target-type})
        ($other $other)))))

!(let $converted (conversion-proposal &math (quote (add (succ zero) (succ zero))))
   (assertEqual (conversion-recheck &math $converted) True))
!(let (quote (Conversion $source $target $type $proof))
      (conversion-proposal &math (quote (add (succ zero) (succ zero))))
   (assertEqual (type:eq &math $target (succ (succ zero))) True))
!(assertEqual (conversion-recheck &math
   (quote (Conversion (add zero zero) (succ zero) nat (refl (add zero zero))))) False)

; Replayed endpoint checks do not inherit rules from a different scope.
!(bind! &bare (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &bare nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(add-atom &bare (: add (-> nat nat nat)))
!(let $converted (conversion-proposal &math (quote (add zero zero)))
   (assertEqual (conversion-recheck &bare $converted) False))
!(assertEqual (car-atom (try (type:normalize &math (add zero (succ zero)) 1))) Incomplete)

; An assumed propositional identity does not make its endpoints convertible.
!(assertEqual (car-atom (judgment-outcome (quote
   (|- ((: A (u 0)) (: x A) (: y A) (: p (id A x y)))
       (convert x y A))))) Refuted)

; The same identity does license explicit dependent transport.
!(assertEqual (judge (quote
   (|- ((: A (u 0)) (: F (-> A (u 0))) (: x A) (: y A)
        (: p (id A x y)) (: value (F x)))
       (: (id:eliminate A x (lam z (lam e (F z))) value y p) (F y))))) True)

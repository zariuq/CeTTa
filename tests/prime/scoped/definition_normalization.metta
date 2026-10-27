(hol:profile base)
!(bind! &thy (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &thy add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; One declaration supplies inspection, typing, conversion and computation.
!(type:of &thy add)
!(type:normalize &thy (add (succ zero) (succ zero)))
!(let (TypeNormalForm $source $value $before $after)
   (type:normalize &thy (add (succ zero) (succ zero)))
   (NormalizationChecks
      (type:check &thy $value $after)
      (type:eq &thy $source $value)
      (type:eq &thy $before $after)))
!(collapse (match &thy (type:rule add $arity $patterns $body)
   (Equation $arity $patterns $body)))

; Object computation cannot execute an ambient equation or effect.
(= (add $x $y) (let $_ (println! unintended-ambient-evaluation) wrong))
!(type:normalize &thy (add zero (succ zero)))
!(type:normalize &thy unknown)
!(car-atom (try (type:normalize &thy (add zero (succ zero)) 1)))
!(type:normalize &thy zero 10000)
!(car-atom (try (type:check &thy zero nat 1)))
!(car-atom (try (type:normalize &thy (add zero nat))))

; Same names in a different workspace do not import the computation rules.
!(bind! &bare (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &bare nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(add-atom &bare (: add (-> nat nat nat)))
!(type:normalize &bare (add zero zero))

; Undeclared rules and data shaped like a result do not acquire authority.
!(add-atom &bare (type:rule add 2 ($x $y) zero))
!(type:normalize &bare (add zero zero))
!(type:eq &bare (add zero zero) zero)

; A definition may return a dependent pair and its proof.
!(hol:define &thy pack (-> (x : nat) (sigma (y : nat) (id nat x y)))
   (= (pack $x) (pair $x (refl $x))))
!(type:normalize &thy (snd (pack (add zero (succ zero)))))

; Universe parameters are instantiated before the computation is checked.
!(hol:define &thy identity (-> (A : (u 0)) (x : A) A)
   (= (identity $A $x) $x))
!(type:normalize &thy (identity nat (succ zero)))
!(add-atom &thy (: opaque-id (-> (A : (u $level)) (-> A A))))
!(type:normalize &thy (opaque-id nat (add zero zero)))
!(type:normalize (u 0))

; Direct publication uses the same optional destination as proof search.
!(type:theorem &thy right-unit
   (-> (n : nat) (id nat (add n zero) n)) (lam n (refl n)))
!(type:check &thy (right-unit zero) (id nat zero zero))
!(car-atom (try (type:of right-unit)))
!(type:theorem &thy wrong-unit
   (-> (n : nat) (id nat (add n zero) (succ n))) (lam n (refl n)))
!(car-atom (try (type:of &thy wrong-unit)))

; Elaboration may fill implicit parameters. The readout still retains the
; authored source while its computed term carries the inferred parameters.
!(bind! &poly (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &poly num (u 0) (: zero num) (: suc (-> num num)))
!(hol:inductive &poly (List (: A set)) set
   (: nil (List A)) (: cons (-> A (List A) (List A))))
!(let (TypeNormalForm $source $value $original-type $normal-type)
   (type:normalize &poly (cons zero nil))
   (ImplicitNormalization (== $source (cons zero nil))
     (type:check &poly $value (List num))))

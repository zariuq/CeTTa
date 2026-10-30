(hol:profile base)

; Public standard APIs are editable. The qualified originals remain stable.
(= (+ $a $b) 42)
!(collapse (+ 1 2))
(= (std:+ $a $b) 99)
(: std:get-type (-> Proc BogusType))
!(add-atom &self (= (std:if $c $a $b) forged))
!(std:+ 1 2)
!(let $definition (std:definitions +) (remove-atom &self $definition))
!(+ 1 2)
!(let $definition (std:definitions +) (add-atom &self $definition))
!(collapse (+ 1 2))

; Mathematical declarations remain checked in their selected context.
!(hol:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &self plus1 (-> nat nat) (= (plus1 $n) (succ $n)))
!(plus1 zero)
; A name the library does not declare may name a constant: the definition
; takes effect, when run and in judgments.
!(hol:define &self and (-> nat nat nat) (= (and $m $n) $m))
!(and (succ zero) zero)
!(type:eq (and (succ zero) zero) (succ zero))
; A theory in a space of its own may name its constants after them, and
; declarations added there are its own.
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(hol:inductive &thy bool (u 0) (: true bool) (: false bool))
!(hol:define &thy and (-> bool bool bool) (= (and true $b) $b) (= (and false $b) false))
!(type:eq &thy (and true false) false)
!(hol:define &thy repr (-> bool bool) (= (repr $b) $b))
!(type:eq &thy (repr true) true)
!(add-atom &thy (: size bool))
!(match &thy (: size $type) $type)

; The words of the term syntax are not reserved: a program may define them,
; and its definition takes effect when run and in judgments.
(= (pair $a $b) (my-pair $a $b))
!(pair 1 2)
!(bind! &tok (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(hol:inductive &tok token (u 0) (: a token) (: b token) (: pair (-> token token token)))
!(hol:define &tok first (-> token token) (= (first a) a) (= (first b) b) (= (first (pair $x $y)) $x))
!(type:eq &tok (first (pair b a)) b)

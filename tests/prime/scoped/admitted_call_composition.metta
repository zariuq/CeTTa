(hol:profile base)
!(hol:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &self add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; Tail position, a let, and a declared wrapper use the same admitted call.
(= (twice $x) (add $x $x))
!(twice (succ zero))
(= (twice-let $x) (let $r (add $x $x) $r))
!(twice-let (succ zero))
(: twice-typed (-> nat nat))
(= (twice-typed $x) (add $x $x))
!(twice-typed (succ zero))
(= (nested $x) (succ (add $x $x)))
!(nested zero)
!(nested (succ zero))

; A quoted call remains data, and an opaque function stays opaque.
(= (quoted $x) (quote (add zero zero)))
!(quoted (succ zero))
(: opaque (-> nat nat))
(= (opaque-wrapper $x) (opaque $x))
!(opaque-wrapper zero)

; A cached body must be reconsidered when its callee acquires an admitted
; definition, even though no ordinary equation for that callee is added.
(= (later-wrapper $x) (later $x))
!(later-wrapper zero)
!(hol:define &self later (-> nat nat) (= (later $x) (succ $x)))
!(later-wrapper zero)

; A stale admitted definition is not made executable by the host boundary.
!(remove-atom &self (: zero nat))
!(twice (succ zero))

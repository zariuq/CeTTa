(hol:profile base)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &math add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; The same scope can be selected directly or passed to an ordinary function.
!(eval (add (succ zero) zero) (admitted &math))
(= (mathematical-call $scope $expression) (eval $expression $scope))
!(mathematical-call (admitted &math) (add zero (succ zero)))
!(let $scope (admitted &math) (eval (add zero (succ zero)) $scope))

; Program equations remain available in the programming view. They cannot
; change the value computed in the admitted view of the same space.
!(add-atom &math (= (add $n zero) zero))
!(eval (add (succ zero) zero) &math)
!(eval (add (succ zero) zero) (admitted &math))
!(eval (add (succ zero) (succ zero)) (admitted &math))

; These code-shaped inputs cannot perform effects in the mathematical scope.
!(eval (add zero (println! forbidden)) (admitted &math))
!(car-atom (try (eval (add zero (succ zero)) (admitted &math 1))))
!(car-atom (try (eval (add zero (succ zero)) (admitted &math 10000))))
!(car-atom (try (eval (add zero nat) (admitted &math))))
!(car-atom (try (eval (add zero zero) (admitted &missing))))
!(car-atom (try (eval (add zero zero) (admitted &math 0))))

; Readback preserves bindings, including shadowing, and exact endpoints.
!(eval (lam (x : nat) (add x zero)) (admitted &math))
!(let $value (eval (lam (x : nat) (lam (y : nat) (add x y))) (admitted &math))
   (type:eq &math $value (lam (a : nat) (lam (b : nat) (add a b)))))
!(let $value (eval (lam (x : nat) (lam (x : nat) (add x zero))) (admitted &math))
   (type:eq &math $value (lam (a : nat) (lam (b : nat) b))))
!(let $value (eval (add zero (succ zero)) (admitted &math))
   (type:eq &math $value zero))

; A normal form may be an opaque application. Passing its data to another
; function does not hand ordinary equations permission to run it.
!(add-atom &math (: opaque (-> nat nat)))
!(add-atom &math (= (opaque $x) (println! forbidden-opaque)))
!(eval (opaque (add zero zero)) (admitted &math))
!(let $v (eval (opaque zero) (admitted &math)) (Observed $v))

; Writing a raw rule does not authorize it.
!(add-atom &math (type:rule opaque 1 ($x) zero))
!(eval (opaque (succ zero)) (admitted &math))

; Dependency removal invalidates a definition; exact restoration permits
; its use again. Unrelated declarations do not revoke it.
!(hol:define &math leaf nat (= leaf zero))
!(hol:define &math middle nat (= middle leaf))
!(eval middle (admitted &math))
!(remove-atom &math (: zero nat))
!(car-atom (try (eval middle (admitted &math))))
!(add-atom &math (: zero nat))
!(eval middle (admitted &math))
!(add-atom &math (: unrelated nat))
!(eval middle (admitted &math))

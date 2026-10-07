(set:profile hol)

; A function into a universe, defined by structural recursion on the numbers,
; computes a type at each number.  Values are checked at the computed types.
; Identity elimination over a motive built from such a function transports
; along a path between numbers into a type that differs from the start type;
; the result is not the method.

!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))

; n-tuples of numbers.
!(set:define &s Tuple (-> num (u 0))
   (= (Tuple zero) num)
   (= (Tuple (suc $n)) (sigma (x : num) (Tuple $n))))
!(type:check &s (Tuple (suc zero)) (u 0))
!(type:eq &s (Tuple (suc (suc zero))) (sigma (x : num) (sigma (y : num) num)))
!(type:check &s zero (Tuple zero))
!(type:check &s (pair zero (pair (suc zero) zero)) (Tuple (suc (suc zero))))
!(type:check &s (pair zero zero) (Tuple (suc (suc zero))))
!(type:check &s (pair zero zero) (Tuple zero))

; The numbers at zero and functions on them at every successor.
!(set:define &s Arity (-> num (u 0))
   (= (Arity zero) num)
   (= (Arity (suc $n)) (-> num num)))
!(type:check &s (lam y (lam e (Arity y))) (-> (y : num) (-> (id num zero y) (u 0))))
!(type:check &s (lam x x) (Arity (suc zero)))
!(type:check &s zero (Arity (suc zero)))

; Along a declared path from zero to one the method zero is transported into
; (-> num num).  Nothing computes on the path, and the result is not zero.
!(add-atom &s (: p01 (id num zero (suc zero))))
!(type:of &s (id:eliminate num zero (lam y (lam e (Arity y))) zero (suc zero) p01))
!(type:check &s (id:eliminate num zero (lam y (lam e (Arity y))) zero (suc zero) p01) (-> num num))
!(type:check &s (id:eliminate num zero (lam y (lam e (Arity y))) zero (suc zero) p01) num)
!(type:eq &s (id:eliminate num zero (lam y (lam e (Arity y))) zero (suc zero) p01) zero)
; At reflexivity it is the method.
!(type:check &s (id:eliminate num zero (lam y (lam e (Arity y))) zero zero (refl zero)) num)
!(type:eq &s (id:eliminate num zero (lam y (lam e (Arity y))) zero zero (refl zero)) zero)

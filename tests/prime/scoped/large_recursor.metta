; The recursor of a declared inductive type takes a motive into any universe.
; With the motive (lam n (u 0)) it computes a type at each number: the numbers
; at zero and functions on them at every successor, or the n-tuples of numbers.
; Values are checked at the computed types.  Identity elimination over the
; large motive transports along a path between numbers; nothing computes on
; the path, and the result is not the method.

!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))

; Small motives compute as before.
!(type:check &s (num-rec (lam n num) zero (lam n (lam h (suc h))) (suc zero)) num)
!(type:eq &s (num-rec (lam n num) zero (lam n (lam h (suc h))) (suc zero)) (suc zero))

; The motive (lam n (u 0)) lives in (-> num (u 1)).
!(type:check &s (lam n (u 0)) (-> num (u 1)))
!(type:check &s (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) zero) (u 0))
!(type:eq &s (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) zero) num)
!(type:eq &s (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero)) (-> num num))
!(type:check &s (lam x x) (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero)))
!(type:check &s zero (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) (suc zero)))

; n-tuples of numbers.
!(type:eq &s (num-rec (lam n (u 0)) num (lam n (lam T (sigma (x : num) T))) (suc (suc zero)))
   (sigma (x : num) (sigma (y : num) num)))
!(type:check &s (pair zero (pair (suc zero) zero))
   (num-rec (lam n (u 0)) num (lam n (lam T (sigma (x : num) T))) (suc (suc zero))))
!(type:check &s (pair zero zero)
   (num-rec (lam n (u 0)) num (lam n (lam T (sigma (x : num) T))) (suc (suc zero))))

; A motive into (u 1) computes types of types.
!(type:check &s (num-rec (lam n (u 1)) (u 0) (lam n (lam T (-> num T))) (suc zero)) (u 1))
!(type:eq &s (num-rec (lam n (u 1)) (u 0) (lam n (lam T (-> num T))) (suc zero)) (-> num (u 0)))

; The large motive under identity elimination, from zero to one.
!(add-atom &s (: p01 (id num zero (suc zero))))
!(type:check &s (lam y (lam e (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) y)))
   (-> (y : num) (-> (id num zero y) (u 0))))
!(type:check &s
   (id:eliminate num zero (lam y (lam e (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) y)))
      zero (suc zero) p01)
   (-> num num))
!(type:eq &s
   (id:eliminate num zero (lam y (lam e (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) y)))
      zero (suc zero) p01)
   zero)
!(type:eq &s
   (id:eliminate num zero (lam y (lam e (num-rec (lam n (u 0)) num (lam n (lam T (-> num num))) y)))
      zero zero (refl zero))
   zero)

; The recursor has the type of each instance of its motive's level.
!(type:check &s num-rec
   (-> (P : (-> num (u 0))) (m1 : (P zero))
       (m2 : (-> (v1 : num) (-> (h1 : (P v1)) (P (suc v1)))))
       (t : num) (P t)))
!(type:check &s num-rec
   (-> (P : (-> num (u 1))) (m1 : (P zero))
       (m2 : (-> (v1 : num) (-> (h1 : (P v1)) (P (suc v1)))))
       (t : num) (P t)))

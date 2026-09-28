!(bind! &thy (new-space))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &thy add (-> nat nat nat) (= (add zero $n) $n) (= (add (succ $m) $n) (succ (add $m $n))))
!(type:check &thy (pair (succ (succ zero)) (refl (succ (succ (succ (succ zero)))))) (sigma (n : nat) (id nat (add n n) (succ (succ (succ (succ (succ zero))))))))

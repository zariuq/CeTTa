!(bind! &thy (new-space))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(type:check &thy (refl zero) (id nat zero (succ zero)))

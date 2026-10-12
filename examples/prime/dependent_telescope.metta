(set:profile hol)

; A dependent result type remembers the exact input witness.
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(add-atom &thy (: Fact (u 0)))
!(add-atom &thy (: fact Fact))
!(add-atom &thy (: other Fact))
!(add-atom &thy (: WitnessOf (-> (z : Fact) (u 0))))
!(add-atom &thy (: remember (-> (p : Fact) (WitnessOf p))))

!(add-atom &thy (: fact-witness (WitnessOf fact)))
!(add-atom &thy (: other-witness (WitnessOf other)))
!(add-atom &thy (: Checked (-> (z : Fact) (u 0))))
!(add-atom &thy (: use-witness
   (-> (p : Fact) (w : (WitnessOf p)) (Checked p))))

!(type:check &thy (remember fact) (WitnessOf fact))
!(type:check &thy (use-witness fact fact-witness) (Checked fact))
!(type:check &thy (use-witness fact other-witness) (Checked fact))
!(type:check &thy (remember fact) (WitnessOf other))

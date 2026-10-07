(set:profile hol)

; A defined call whose equation unfolds to the language's identity
; eliminator, stuck on a path that is not reflexivity, is still a term the
; judgments compare: it is reflexively equal to itself, and not to another.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: k num))
!(add-atom &s (: q (id num zero k)))
!(set:define &s along (-> (y : num) (e : (id num zero y)) num)
   (= (along $y $e) (id:eliminate num zero (lam y (lam e num)) zero $y $e)))
!(type:check &s (along k q) num)
!(type:check &s (refl (along k q)) (id num (along k q) (along k q)))
!(type:eq &s (along k q) (along k q))
!(type:eq &s (along k q) zero)

; Transport between types, defined through the eliminator, in an open context.
!(add-atom &s (: A (u 0)))
!(add-atom &s (: B (u 0)))
!(add-atom &s (: p (id (u 0) A B)))
!(add-atom &s (: a A))
!(set:define &s transport
   (-> (X : (u 0)) (Y : (u 0)) (r : (id (u 0) X Y)) (d : X) Y)
   (= (transport $X $Y $r $d) (id:eliminate (u 0) $X (lam Z (lam e Z)) $d $Y $r)))
!(type:check &s (refl (transport A B p a)) (id B (transport A B p a) (transport A B p a)))
!(type:check &s (lam d (refl (transport A B p d))) (-> (d : A) (id B (transport A B p d) (transport A B p d))))
!(type:eq &s (transport A A (refl A) a) a)

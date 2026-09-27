; The type `type:of` gives names every binder its codomain refers to, so no
; de Bruijn index shows where a name can stand: a polymorphic constant's type
; reads (-> (x : (u 0)) (-> x x)).  A binder nothing refers to stays unnamed,
; and a name is never one of the term's own constants.  The type read back
; is the type given.
!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: polyId (-> (A : (u $l)) (-> A A))))
!(type:of &s polyId)
!(type:of &s (polyId num))
!(add-atom &s (: refl-at (-> (n : num) (id num n n))))
!(type:of &s refl-at)
!(type:check &s refl-at (-> (m : num) (id num m m)))
; A constant spelled like a would-be binder name is not captured.
!(add-atom &s (: x num))
!(add-atom &s (: const-x (-> (n : num) (id num n x))))
!(type:of &s const-x)
!(type:of &s id:eliminate)
!(type:of (lam (y : u0) y))

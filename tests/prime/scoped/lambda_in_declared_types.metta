(set:profile hol)

; A declared type may hold an unannotated lambda applied to arguments, as
; the type of a checked term may.  The declaration is read and formed, and a
; definition's equations are checked against it.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:define &s lam-in-type (-> (X : (u 0)) (d : X) (id X ((lam Z Z) d) d))
   (= (lam-in-type $X $d) (refl $d)))
!(set:define &s lam-in-type2 (-> (X : (u 0)) (d : X) (id X ((lam Z (lam e Z)) d X) d))
   (= (lam-in-type2 $X $d) (refl $d)))
!(add-atom &s (: A (u 0)))
!(add-atom &s (: a A))
!(add-atom &s (: b A))
!(type:check &s (lam-in-type A a) (id A a a))
!(type:check &s (lam-in-type2 A a) (id A a a))
; The equation must meet the declared type: refl a does not prove a = b.
!(let $r (try (set:define &s lam-wrong (-> (X : (u 0)) (d : X) (e : X) (id X ((lam Z Z) d) e))
                (= (lam-wrong $X $d $e) (refl $d))))
   (car-atom $r))

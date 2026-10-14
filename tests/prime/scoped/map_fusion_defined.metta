(hol:profile core)

; Map fusion with map defined, as Agda writes it:
;   map-compose []       = refl
;   map-compose (x ∷ xs) = cong (f (g x) ∷_) (map-compose xs)

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile core)) $profile-space)))
!(pf:theory &thy ((: elem (u 0)) (: f (-> elem elem)) (: g (-> elem elem))))
!(hol:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(pf:equality &thy list refl@list subst@list)
; the induction principle is derived from the declaration
!(hol:known-proposition &thy list-ind)
!(hol:define &thy mapf (-> (-> elem elem) list list)
   (= (mapf $h nil) nil)
   (= (mapf $h (cons $x $xs)) (cons ($h $x) (mapf $h $xs))))

!(pf:theorem &thy map-fusion
   (all list (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs))))
   (pf:induction list-ind (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs)))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:cong list list (lam r (cons (f (g x)) r))
          (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by ih)))))))

!(match &thy (hol:known map-fusion $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))
!(pf:theory &thy ((: ys list)))
!(hol:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (f (g y))) ys)))
; With the composition reversed, matching finds no instance and nothing rules
; one out: the list sits only under the recursive mapf, where an instance
; could still compute to the goal.  The judgment is left undetermined.
!(hol:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (g (f y))) ys)))

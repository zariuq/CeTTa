; Map fusion with map defined, as Agda writes it:
;   map-compose []       = refl
;   map-compose (x ∷ xs) = cong (f (g x) ∷_) (map-compose xs)

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0)) (: f (-> elem elem)) (: g (-> elem elem))))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(pf:equality &thy list refl@list subst@list)
; the induction principle is derived from the declaration
!(set:known-proposition &thy list-ind)
!(set:define &thy mapf (-> (-> elem elem) list list)
   (= (mapf $h nil) nil)
   (= (mapf $h (cons $x $xs)) (cons ($h $x) (mapf $h $xs))))

!(pf:theorem &thy map-fusion
   (all list (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs))))
   (pf:induction list-ind (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs)))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:cong list list (lam r (cons (f (g x)) r))
          (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by ih)))))))

!(match &thy (set:known map-fusion $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))
!(pf:theory &thy ((: ys list)))
!(set:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (f (g y))) ys)))
!(set:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (g (f y))) ys)))

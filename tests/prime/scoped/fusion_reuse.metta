; Proof reuse across a rewrite, inside the extensional layer.  A property
; established of the unfused program is carried to the fused program by the
; published fusion theorem and substitution, not rediscovered by testing the
; optimized output; the derived record keeps its dependencies, and the
; original proof stays inspectable on request.
!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0)) (: f (-> elem elem)) (: g (-> elem elem))))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(pf:equality &thy list refl@list subst@list)
!(pf:equality &thy nat refl@nat subst@nat)
!(set:define &thy mapf (-> (-> elem elem) list list)
   (= (mapf $h nil) nil)
   (= (mapf $h (cons $x $xs)) (cons ($h $x) (mapf $h $xs))))
!(set:define &thy length (-> list nat)
   (= (length nil) zero)
   (= (length (cons $x $xs)) (succ (length $xs))))
; the fusion theorem, by induction
!(pf:theorem &thy map-fusion
   (all list (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs))))
   (pf:induction list-ind (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs)))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:cong list list (lam r (cons (f (g x)) r))
          (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by ih)))))))
; a property of the unfused program: mapping preserves length, twice
!(pf:theorem &thy length-map (all (-> elem elem) (lam h (all list (lam xs (eq nat (length (mapf h xs)) (length xs))))))
   (pf:fix h (pf:induction list-ind (lam xs (eq nat (length (mapf h xs)) (length xs)))
     (pf:refl nat zero)
     (pf:fix x (pf:fix xs (pf:assume ih (pf:cong nat nat (lam r (succ r)) (length (mapf h xs)) (length xs) ih)))))))
!(pf:theorem &thy length-unfused (all list (lam xs (eq nat (length (mapf f (mapf g xs))) (length xs))))
   (pf:fix xs (pf:trans nat (length (mapf f (mapf g xs))) (length (mapf g xs)) (length xs)
     (pf:by length-map) (pf:by length-map))))
; reuse: the property of the fused program follows by substitution along
; fusion, with no induction and no evaluation of the fused program
!(pf:theorem &thy length-fused (all list (lam xs (eq nat (length (mapf (lam y (f (g y))) xs)) (length xs))))
   (pf:fix xs (pf:subst-with list (lam l (eq nat (length l) (length xs)))
     (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by map-fusion) (pf:by length-unfused))))
; the general shape: any predicate on the unfused output holds of the fused output
!(pf:theorem &thy reuse-any
   (all (-> list prop) (lam P (all list (lam xs (imp (P (mapf f (mapf g xs))) (P (mapf (lam y (f (g y))) xs)))))))
   (pf:fix P (pf:fix xs (pf:assume w (pf:subst-with list P (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by map-fusion) w)))))
; the derived record depends on the theorem it reused, and its original
; proof is retained for inspection
!(match &thy (set:known length-fused $p $o $proof $depends $r $s)
   (let $names (map-atom $depends $d (car-atom $d)) $names))
!(let $p (set:known-proof &thy length-fused) (car-atom $p))
; the reverse direction is not obtained by the same substitution: the
; equation runs the other way, and the checker says so
!(pf:proves &thy
   (pf:fix xs (pf:subst-with list (lam l (eq nat (length l) (length xs)))
     (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by map-fusion) (pf:by length-unfused)))
   (all list (lam xs (eq nat (length (mapf f (mapf g xs))) (length xs)))))

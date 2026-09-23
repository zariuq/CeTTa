; Map fusion the way it is written in Agda:
;   map-compose []       = refl
;   map-compose (x ∷ xs) = cong (f (g x) ∷_) (map-compose xs)
; Here composition is a lambda, so the kernel's conversion does the
; unfolding that Agda does by computation; the map equations are axioms of
; the theory since there is no definition form yet.

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0)) (: list (u 0))
                  (: nil list) (: cons (-> elem list list))
                  (: mapf (-> (-> elem elem) list list))
                  (: f (-> elem elem)) (: g (-> elem elem))))
!(pf:equality &thy list refl@list subst@list)
!(set:axiom &thy map-nil (all (-> elem elem) (lam h (eq list (mapf h nil) nil))))
!(set:axiom &thy map-cons (all (-> elem elem) (lam h (all elem (lam x (all list (lam xs
   (eq list (mapf h (cons x xs)) (cons (h x) (mapf h xs))))))))))
!(set:axiom &thy list-ind (all (-> list prop) (lam P
   (imp (P nil)
        (imp (all elem (lam x (all list (lam xs (imp (P xs) (P (cons x xs)))))))
             (all list (lam xs (P xs))))))))

!(pf:theorem &thy map-fusion
   (all list (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs))))
   (pf:induction list-ind (lam xs (eq list (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs)))
     (pf:calc list (mapf f (mapf g nil))
       (= (mapf f nil)              (cong list (lam r (mapf f r)) (mapf g nil) nil (pf:by map-nil)))
       (= nil                       (by map-nil))
       (= (mapf (lam y (f (g y))) nil) (sym (by map-nil))))
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:calc list (mapf f (mapf g (cons x xs)))
         (= (mapf f (cons (g x) (mapf g xs)))              (cong list (lam r (mapf f r)) (mapf g (cons x xs)) (cons (g x) (mapf g xs)) (pf:by map-cons)))
         (= (cons (f (g x)) (mapf f (mapf g xs)))          (by map-cons))
         (= (cons (f (g x)) (mapf (lam y (f (g y))) xs))   (cong list (lam r (cons (f (g x)) r)) (mapf f (mapf g xs)) (mapf (lam y (f (g y))) xs) (pf:by ih)))
         (= (mapf (lam y (f (g y))) (cons x xs))           (sym (by map-cons)))))))))

; The record, a use, and the reversed composition refuted at the same list.
!(match &thy (set:known map-fusion $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))
!(pf:theory &thy ((: ys list)))
!(set:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (f (g y))) ys)))
; Who decided the conversions of that use: the kernel, or the structural fallback.
!(let $r (try (set:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (f (g y))) ys)))) (let $e (index-atom $r 1) (index-atom $e 2)))
!(set:proves &thy (pf:by map-fusion) (eq list (mapf f (mapf g ys)) (mapf (lam y (g (f y))) ys)))

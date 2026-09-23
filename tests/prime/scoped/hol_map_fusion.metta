; Higher-order mathematics in the set: proof language: list-map fusion,
;   forall xs. mapf f (mapf g xs) = mapf (comp f g) xs
; for symbolic functions f and g, proved once by the predicate-quantified
; induction principle of an authored list theory, with equational reasoning
; through explicit congruence axioms.  Coverage: one carrier `elem`, lists
; over it, and functions elem -> elem; no definitional unfolding exists, so
; the map equations are assumptions of the theory, not computation.

(: elem (u 0))
(: list (u 0))
(: nil list)
(: cons (-> elem (-> list list)))
(: mapf (-> (-> elem elem) (-> list list)))
(: comp (-> (-> elem elem) (-> (-> elem elem) (-> elem elem))))
(: f (-> elem elem))
(: g (-> elem elem))

!(set:axiom map-nil (all (-> elem elem) (lam h (eq list (mapf h nil) nil))))
!(set:axiom map-cons (all (-> elem elem) (lam h (all elem (lam x (all list (lam xs
   (eq list (mapf h (cons x xs)) (cons (h x) (mapf h xs))))))))))
!(set:axiom comp-apply (all elem (lam x (eq elem (comp f g x) (f (g x))))))
!(set:axiom eq-trans (all list (lam a (all list (lam b (all list (lam c
   (imp (eq list a b) (imp (eq list b c) (eq list a c))))))))))
!(set:axiom eq-sym (all list (lam a (all list (lam b (imp (eq list a b) (eq list b a)))))))
!(set:axiom eq-sym-elem (all elem (lam a (all elem (lam b (imp (eq elem a b) (eq elem b a)))))))
!(set:axiom map-cong (all (-> elem elem) (lam h (all list (lam xs (all list (lam ys
   (imp (eq list xs ys) (eq list (mapf h xs) (mapf h ys))))))))))
!(set:axiom cons-cong (all elem (lam x (all elem (lam y (all list (lam xs (all list (lam ys
   (imp (eq elem x y) (imp (eq list xs ys) (eq list (cons x xs) (cons y ys)))))))))))))
!(set:axiom list-ind (all (-> list prop) (lam P
   (imp (P nil)
        (imp (all elem (lam x (all list (lam xs (imp (P xs) (P (cons x xs)))))))
             (all list (lam xs (P xs))))))))

; The theorem, proved by induction with the predicate supplied once.
!(set:theorem map-fusion
   (all list (lam xs (eq list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:known list-ind)
                    (lam xs (eq list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
       ; base: mapf f (mapf g nil) = mapf f nil = nil = mapf (comp f g) nil
       (pf:imp-elim
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans)
                          (mapf f (mapf g nil))) (mapf f nil)) (mapf (comp f g) nil))
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known map-cong) f)
                                                  (mapf g nil)) nil)
                        (pf:all-elim (pf:known map-nil) g)))
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans)
                            (mapf f nil)) nil) (mapf (comp f g) nil))
             (pf:all-elim (pf:known map-nil) f))
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) (mapf (comp f g) nil)) nil)
                        (pf:all-elim (pf:known map-nil) (comp f g))))))
     ; step: for x, xs, from IH: mapf f (mapf g xs) = mapf (comp f g) xs
     (pf:all-intro (pf:all-intro (pf:imp-intro
       ; mapf f (mapf g (cons x xs))
       ;   = mapf f (cons (g x) (mapf g xs))              [map-cong f, map-cons g]
       ;   = cons (f (g x)) (mapf f (mapf g xs))          [map-cons f]
       ;   = cons (comp f g x) (mapf (comp f g) xs)       [cons-cong: comp-apply sym, IH]
       ;   = mapf (comp f g) (cons x xs)                  [map-cons (comp f g), sym]
       (pf:imp-elim
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans)
                          (mapf f (mapf g (cons (pf:var 1) (pf:var 0)))))
                          (mapf f (cons (g (pf:var 1)) (mapf g (pf:var 0)))))
                          (mapf (comp f g) (cons (pf:var 1) (pf:var 0))))
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known map-cong) f)
                                                  (mapf g (cons (pf:var 1) (pf:var 0))))
                                                  (cons (g (pf:var 1)) (mapf g (pf:var 0))))
                        (pf:all-elim (pf:all-elim (pf:all-elim (pf:known map-cons) g) (pf:var 1)) (pf:var 0))))
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans)
                            (mapf f (cons (g (pf:var 1)) (mapf g (pf:var 0)))))
                            (cons (f (g (pf:var 1))) (mapf f (mapf g (pf:var 0)))))
                            (mapf (comp f g) (cons (pf:var 1) (pf:var 0))))
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known map-cons) f) (g (pf:var 1))) (mapf g (pf:var 0))))
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans)
                              (cons (f (g (pf:var 1))) (mapf f (mapf g (pf:var 0)))))
                              (cons (comp f g (pf:var 1)) (mapf (comp f g) (pf:var 0))))
                              (mapf (comp f g) (cons (pf:var 1) (pf:var 0))))
               (pf:imp-elim
                 (pf:imp-elim
                   (pf:all-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known cons-cong)
                                  (f (g (pf:var 1)))) (comp f g (pf:var 1)))
                                  (mapf f (mapf g (pf:var 0)))) (mapf (comp f g) (pf:var 0)))
                   (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym-elem)
                                               (comp f g (pf:var 1))) (f (g (pf:var 1))))
                                (pf:all-elim (pf:known comp-apply) (pf:var 1))))
                 (pf:hyp 0)))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym)
                                         (mapf (comp f g) (cons (pf:var 1) (pf:var 0))))
                                         (cons (comp f g (pf:var 1)) (mapf (comp f g) (pf:var 0))))
                          (pf:all-elim (pf:all-elim (pf:all-elim (pf:known map-cons) (comp f g)) (pf:var 1)) (pf:var 0)))))))))))

; The retained derivation crosses the dependent proof-family boundary.  The
; package is accepted only after the regular kernel checks the compiled term;
; its nine mathematical axioms remain explicit assumptions.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof map-fusion)
      (NativeProof $name (size-atom $assumptions)))

; Ordinary application: instantiate the theorem at a symbolic list.
(: ys list)
!(set:proves (pf:all-elim (pf:known map-fusion) ys)
             (eq list (mapf f (mapf g ys)) (mapf (comp f g) ys)))
; Retained proof inspection: the induction is visible, not an assumed axiom.
!(let $p (set:known-proof map-fusion) (car-atom $p))
!(match &self (set:known map-fusion $p $origin $proof $depends $rev $sig) (Origin $origin))
!(match &self (set:known map-fusion $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))

; Negative control, pinned independently: reversing the composition passes
; every empty-list sample and every f = g sample, yet fails for noncommuting
; Boolean functions on a nonempty list.  Executable version on MeTTa lists:
(= (map $h ()) ())
(= (map $h ($x . $xs)) (($h $x) . (map $h $xs)))
; map of a composition, written directly since functions are first order here
(= (map-comp $h $k ()) ())
(= (map-comp $h $k ($x . $xs)) (($h ($k $x)) . (map-comp $h $k $xs)))
(= (not-b True) False)
(= (not-b False) True)
(= (const-true $x) True)
!(map not-b (map const-true (True . ())))       ; fusion, left side
!(map-comp not-b const-true (True . ()))        ; fusion, right side: agrees
!(map-comp const-true not-b (True . ()))        ; reversed composition: differs
!(map not-b (map const-true ()))                ; empty sample
!(map-comp const-true not-b ())                 ; reversed passes the empty sample
!(map not-b (map not-b (True . ())))            ; f = g sample
!(map-comp not-b not-b (True . ()))             ; reversed passes the diagonal sample
; The reversed statement is not provable by the same argument: the proof's
; final step needs comp g f x = f (g x), which no axiom supplies.
!(let $r (try (set:proves
   (pf:all-elim (pf:known map-fusion) ys)
   (eq list (mapf f (mapf g ys)) (mapf (comp g f) ys)))) (car-atom $r))

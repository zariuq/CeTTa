; The reversal theorem again, with append and rev DEFINED by their equations
; instead of assumed.  A definition is admitted only as a total structural
; recursion over the constructors named by the induction axiom; its equations
; then unfold during conversion, so the chains shrink to what Agda writes:
;   app-nil []       = refl              app-nil (x ∷ xs) = cong (x ∷_) (app-nil xs)

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0))))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(pf:equality &thy list refl@list subst@list)
; the induction principle is derived from the declaration
!(set:known-proposition &thy list-ind)
!(set:define &thy append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $x $xs) $ys) (cons $x (append $xs $ys))))
!(set:define &thy rev (-> list list)
   (= (rev nil) nil)
   (= (rev (cons $x $xs)) (append (rev $xs) (cons $x nil))))
; What a definition records, and two definitions the checker refuses:
; a missing case, and a recursion that is not structural.
!(set:known-proposition &thy rev)
!(set:define &thy half (-> list list) (= (half nil) nil))
!(set:define &thy spin (-> list list)
   (= (spin nil) nil)
   (= (spin (cons $x $xs)) (spin (cons $x $xs))))

; lemma app_Nil2: xs @ [] = xs
!(pf:theorem &thy append-nil-right (all list (lam xs (eq list (append xs nil) xs)))
   (pf:induction list-ind (lam xs (eq list (append xs nil) xs))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:cong list list (lam r (cons x r)) (append xs nil) xs ih))))))

; lemma app_assoc
!(pf:theorem &thy append-assoc
   (all list (lam xs (all list (lam ys (all list (lam zs
     (eq list (append (append xs ys) zs) (append xs (append ys zs)))))))))
   (pf:induction list-ind
     (lam xs (all list (lam ys (all list (lam zs
       (eq list (append (append xs ys) zs) (append xs (append ys zs))))))))
     (pf:fix ys (pf:fix zs (pf:refl list (append ys zs))))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix ys (pf:fix zs
       (pf:cong list list (lam r (cons x r))
          (append (append xs ys) zs) (append xs (append ys zs)) (pf:by ih)))))))))

; lemma rev_app: rev (xs @ ys) = rev ys @ rev xs
!(pf:theorem &thy rev-append
   (all list (lam xs (all list (lam ys (eq list (rev (append xs ys)) (append (rev ys) (rev xs)))))))
   (pf:induction list-ind
     (lam xs (all list (lam ys (eq list (rev (append xs ys)) (append (rev ys) (rev xs))))))
     (pf:fix ys (pf:sym list (append (rev ys) nil) (rev ys) (pf:by append-nil-right)))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix ys
       (pf:calc list (rev (append (cons x xs) ys))
         (= (append (append (rev ys) (rev xs)) (cons x nil))  (cong list (lam r (append r (cons x nil))) (rev (append xs ys)) (append (rev ys) (rev xs)) (pf:by ih)))
         (= (append (rev ys) (append (rev xs) (cons x nil)))  (by append-assoc)))))))))

; theorem rev_rev: rev (rev xs) = xs
!(pf:theorem &thy rev-rev (all list (lam xs (eq list (rev (rev xs)) xs)))
   (pf:induction list-ind (lam xs (eq list (rev (rev xs)) xs))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:calc list (rev (rev (cons x xs)))
         (= (append (rev (cons x nil)) (rev (rev xs)))  (by rev-append))
         (= (cons x xs)                                 (cong list (lam r (cons x r)) (rev (rev xs)) xs (pf:by ih)))))))))

; The record: its dependencies now include the two definitions.
!(match &thy (set:known rev-rev $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))
!(pf:theory &thy ((: ys list)))
!(set:proves &thy (pf:by rev-rev) (eq list (rev (rev ys)) ys))
!(set:proves &thy (pf:by rev-rev) (eq list (rev ys) ys))
; A computation by conversion alone, with no lemma: two reversals of a
; concrete list are decided by unfolding.
!(pf:theory &thy ((: a elem) (: b elem)))
!(pf:proves &thy (pf:refl list (cons a (cons b nil))) (eq list (rev (rev (cons a (cons b nil)))) (cons a (cons b nil))))
!(pf:proves &thy (pf:refl list (cons b (cons a nil))) (eq list (rev (cons a (cons b nil))) (cons b (cons a nil))))
!(pf:proves &thy (pf:refl list (cons a (cons b nil))) (eq list (rev (cons a (cons b nil))) (cons a (cons b nil))))
; Withdrawing a definition makes every fact that mentions it abstain on reuse.
!(match &thy (set:known rev $p $o $f $d $r $s) (remove-atom &thy (set:known rev $p $o $f $d $r $s)))
!(set:proves &thy (pf:by rev-rev) (eq list (rev (rev ys)) ys))
!(let $r (try (set:proves &thy (pf:by rev-rev) (eq list (rev (rev ys)) ys))) (let $e (index-atom $r 1) (car-atom $e)))

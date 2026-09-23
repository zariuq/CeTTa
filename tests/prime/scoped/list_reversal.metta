; The first theorem of the Isabelle tutorial, rev (rev xs) = xs, written the
; way it reads there: four lemmas by induction, each an equational chain.
; The object theory lives in its own space; the program space holds only the
; proof verbs.  The list functions are given by their equations as axioms
; (there is no definition form yet); every other equality step is derived.

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0)) (: list (u 0))
                  (: nil list) (: cons (-> elem list list))
                  (: append (-> list list list)) (: rev (-> list list))))
!(pf:equality &thy list refl@list subst@list)
!(set:axiom &thy append-nil (all list (lam ys (eq list (append nil ys) ys))))
!(set:axiom &thy append-cons (all elem (lam x (all list (lam xs (all list (lam ys
   (eq list (append (cons x xs) ys) (cons x (append xs ys))))))))))
!(set:axiom &thy rev-nil (eq list (rev nil) nil))
!(set:axiom &thy rev-cons (all elem (lam x (all list (lam xs
   (eq list (rev (cons x xs)) (append (rev xs) (cons x nil))))))))
!(set:axiom &thy list-ind (all (-> list prop) (lam P
   (imp (P nil)
        (imp (all elem (lam x (all list (lam xs (imp (P xs) (P (cons x xs)))))))
             (all list (lam xs (P xs))))))))

; lemma app_Nil2: xs @ [] = xs
!(pf:theorem &thy append-nil-right (all list (lam xs (eq list (append xs nil) xs)))
   (pf:induction list-ind (lam xs (eq list (append xs nil) xs))
     (pf:by append-nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:calc list (append (cons x xs) nil)
         (= (cons x (append xs nil)) (by append-cons))
         (= (cons x xs)              (cong list (lam r (cons x r)) (append xs nil) xs ih))))))))

; lemma app_assoc: (xs @ ys) @ zs = xs @ (ys @ zs)
!(pf:theorem &thy append-assoc
   (all list (lam xs (all list (lam ys (all list (lam zs
     (eq list (append (append xs ys) zs) (append xs (append ys zs)))))))))
   (pf:induction list-ind
     (lam xs (all list (lam ys (all list (lam zs
       (eq list (append (append xs ys) zs) (append xs (append ys zs))))))))
     (pf:fix ys (pf:fix zs
       (pf:calc list (append (append nil ys) zs)
         (= (append ys zs)              (cong list (lam r (append r zs)) (append nil ys) ys (pf:by append-nil)))
         (= (append nil (append ys zs)) (sym (by append-nil))))))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix ys (pf:fix zs
       (pf:calc list (append (append (cons x xs) ys) zs)
         (= (append (cons x (append xs ys)) zs)   (cong list (lam r (append r zs)) (append (cons x xs) ys) (cons x (append xs ys)) (pf:by append-cons)))
         (= (cons x (append (append xs ys) zs))   (by append-cons))
         (= (cons x (append xs (append ys zs)))   (cong list (lam r (cons x r)) (append (append xs ys) zs) (append xs (append ys zs)) (pf:by ih)))
         (= (append (cons x xs) (append ys zs))   (sym (by append-cons)))))))))))

; lemma rev_app: rev (xs @ ys) = rev ys @ rev xs
!(pf:theorem &thy rev-append
   (all list (lam xs (all list (lam ys (eq list (rev (append xs ys)) (append (rev ys) (rev xs)))))))
   (pf:induction list-ind
     (lam xs (all list (lam ys (eq list (rev (append xs ys)) (append (rev ys) (rev xs))))))
     (pf:fix ys
       (pf:calc list (rev (append nil ys))
         (= (rev ys)                    (cong list (lam r (rev r)) (append nil ys) ys (pf:by append-nil)))
         (= (append (rev ys) nil)       (sym (by append-nil-right)))
         (= (append (rev ys) (rev nil)) (cong list (lam r (append (rev ys) r)) nil (rev nil) (pf:sym list (rev nil) nil (pf:by rev-nil))))))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix ys
       (pf:calc list (rev (append (cons x xs) ys))
         (= (rev (cons x (append xs ys)))                        (cong list (lam r (rev r)) (append (cons x xs) ys) (cons x (append xs ys)) (pf:by append-cons)))
         (= (append (rev (append xs ys)) (cons x nil))           (by rev-cons))
         (= (append (append (rev ys) (rev xs)) (cons x nil))     (cong list (lam r (append r (cons x nil))) (rev (append xs ys)) (append (rev ys) (rev xs)) (pf:by ih)))
         (= (append (rev ys) (append (rev xs) (cons x nil)))     (by append-assoc))
         (= (append (rev ys) (rev (cons x xs)))                  (cong list (lam r (append (rev ys) r)) (append (rev xs) (cons x nil)) (rev (cons x xs)) (pf:sym list (rev (cons x xs)) (append (rev xs) (cons x nil)) (pf:by rev-cons)))))))))))

; theorem rev_rev: rev (rev xs) = xs
!(pf:theorem &thy rev-rev (all list (lam xs (eq list (rev (rev xs)) xs)))
   (pf:induction list-ind (lam xs (eq list (rev (rev xs)) xs))
     (pf:calc list (rev (rev nil))
       (= (rev nil) (cong list (lam r (rev r)) (rev nil) nil (pf:by rev-nil)))
       (= nil       (by rev-nil)))
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:calc list (rev (rev (cons x xs)))
         (= (rev (append (rev xs) (cons x nil)))          (cong list (lam r (rev r)) (rev (cons x xs)) (append (rev xs) (cons x nil)) (pf:by rev-cons)))
         (= (append (rev (cons x nil)) (rev (rev xs)))     (by rev-append))
         (= (append (rev (cons x nil)) xs)                 (cong list (lam r (append (rev (cons x nil)) r)) (rev (rev xs)) xs (pf:by ih)))
         (= (append (append (rev nil) (cons x nil)) xs)    (cong list (lam r (append r xs)) (rev (cons x nil)) (append (rev nil) (cons x nil)) (pf:by rev-cons)))
         (= (append (append nil (cons x nil)) xs)          (cong list (lam r (append (append r (cons x nil)) xs)) (rev nil) nil (pf:by rev-nil)))
         (= (append (cons x nil) xs)                       (cong list (lam r (append r xs)) (append nil (cons x nil)) (cons x nil) (pf:by append-nil)))
         (= (cons x (append nil xs))                       (by append-cons))
         (= (cons x xs)                                    (cong list (lam r (cons x r)) (append nil xs) xs (pf:by append-nil)))))))))

; What was recorded: the theorem, its origin, and what it depends on.
!(match &thy (set:known rev-rev $p $origin $proof $depends $rev $sig)
   (let $first (car-atom $depends) (Depends (size-atom $depends) first (car-atom $first))))
; A use of the theorem at a symbolic list, and a wrong claim refuted.
!(pf:theory &thy ((: ys list)))
!(set:proves &thy (pf:by rev-rev) (eq list (rev (rev ys)) ys))
; Who decided the conversions of that use: the kernel, or the structural fallback.
!(let $r (try (set:proves &thy (pf:by rev-rev) (eq list (rev (rev ys)) ys))) (let $e (index-atom $r 1) (index-atom $e 2)))
!(set:proves &thy (pf:by rev-rev) (eq list (rev ys) ys))

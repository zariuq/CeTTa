(set:profile hol)

; Two reversals of a list, on the face of sets.  The definitions add their
; equations to the set: language, which the checker uses as computation.
; Here "rev l = rev-onto nil l" is proved for every list, through the lemma
; "rev-onto acc l = append (rev l) acc", proved by induction on l; an invalid
; proof of the lemma is not accepted.  A wrong reversal, which puts each
; number back in front, is proved to be the identity, and the statement
; that it is rev is refuted at the list [0, 1].
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))
!(set:define &self rev (-> list list)
   (= (rev nil) nil)
   (= (rev (cons $a $as)) (append (rev $as) (cons $a nil))))
!(set:define &self rev-onto (-> list list list)
   (= (rev-onto $acc nil) $acc)
   (= (rev-onto $acc (cons $x $xs)) (rev-onto (cons $x $acc) $xs)))
!(set:define &self wrong-rev (-> list list)
   (= (wrong-rev nil) nil)
   (= (wrong-rev (cons $a $as)) (append (cons $a nil) (wrong-rev $as))))
!(set:known-proposition &self list-ind)
!(pf:equality &self list refl@list subst@list)

; ---- Two facts about append, by induction on the first list.
!(pf:theorem &self append-nil (all list (lam l (eq list (append l nil) l)))
   (pf:induction list-ind (lam l (eq list (append l nil) l))
     (pf:refl list nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong list list (lam r (cons a r)) (append as nil) as ih))))))
!(pf:theorem &self append-assoc
   (all list (lam xs (all list (lam ys (all list (lam zs
     (eq list (append (append xs ys) zs) (append xs (append ys zs)))))))))
   (pf:induction list-ind
     (lam xs (all list (lam ys (all list (lam zs
       (eq list (append (append xs ys) zs) (append xs (append ys zs))))))))
     (pf:fix ys (pf:fix zs (pf:refl list (append ys zs))))
     (pf:fix a (pf:fix as (pf:assume ih (pf:fix ys (pf:fix zs
       (pf:cong list list (lam r (cons a r))
          (append (append as ys) zs) (append as (append ys zs)) (pf:by ih)))))))))

; ---- The lemma, by induction on l for every accumulator.  At the empty
; list both sides compute to acc.  At cons x xs the left side computes to
; rev-onto (cons x acc) xs: the hypothesis at the longer accumulator
; cons x acc gives append (rev xs) (cons x acc), and associativity, read
; backwards, gives append (append (rev xs) (cons x nil)) acc, the right side.
!(pf:theorem &self rev-onto-append
   (all list (lam l (all list (lam acc (eq list (rev-onto acc l) (append (rev l) acc))))))
   (pf:induction list-ind
     (lam l (all list (lam acc (eq list (rev-onto acc l) (append (rev l) acc)))))
     (pf:fix acc (pf:refl list acc))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix acc
       (pf:calc list (rev-onto acc (cons x xs))
         (= (append (rev xs) (cons x acc)) (pf:all-elim ih (cons x acc)))
         (= (append (append (rev xs) (cons x nil)) acc) (sym (by append-assoc))))))))))
; The theorem: append-nil read backwards, then the lemma read backwards, its
; instance at l and the empty accumulator found by pf:by.
!(pf:theorem &self rev-is-rev-onto
   (all list (lam l (eq list (rev l) (rev-onto nil l))))
   (pf:fix l
     (pf:calc list (rev l)
       (= (append (rev l) nil) (sym (by append-nil)))
       (= (rev-onto nil l) (sym (by rev-onto-append))))))
!(set:known-proposition &self rev-is-rev-onto)
; The theorem used at the list [0, 1]: pf:by finds the instance from the
; goal, and the same instance can be given explicitly.
!(pf:proves &self (pf:by rev-is-rev-onto)
   (eq list (rev (cons zero (cons (suc zero) nil))) (rev-onto nil (cons zero (cons (suc zero) nil)))))
!(pf:proves &self (pf:all-elim (pf:known rev-is-rev-onto) (cons zero (cons (suc zero) nil)))
   (eq list (rev (cons zero (cons (suc zero) nil))) (rev-onto nil (cons zero (cons (suc zero) nil)))))
; The lemma and associativity, each used at closed lists.
!(pf:proves &self (pf:by rev-onto-append)
   (eq list (rev-onto (cons zero nil) (cons (suc zero) nil))
            (append (rev (cons (suc zero) nil)) (cons zero nil))))
!(pf:proves &self (pf:by append-assoc)
   (eq list (append (append (cons zero nil) (cons (suc zero) nil)) nil)
            (append (cons zero nil) (append (cons (suc zero) nil) nil))))
; At a goal that is not an instance, rev [0, 1] against rev-onto nil [1, 0],
; the instance the goal fixes, l = [0, 1], does not give its right side:
; pf:by does not prove it, and the judgment stays undecided.
!(pf:proves &self (pf:by rev-is-rev-onto)
   (eq list (rev (cons zero (cons (suc zero) nil))) (rev-onto nil (cons (suc zero) (cons zero nil)))))

; An invalid proof of the lemma: a step that uses the hypothesis at the same
; accumulator proves a statement about rev-onto acc xs, not about
; rev-onto acc (cons x xs).
!(pf:proves &self
   (pf:induction list-ind
     (lam l (all list (lam acc (eq list (rev-onto acc l) (append (rev l) acc)))))
     (pf:fix acc (pf:refl list acc))
     (pf:fix x (pf:fix xs (pf:assume ih (pf:fix acc (pf:all-elim ih acc))))))
   (all list (lam l (all list (lam acc (eq list (rev-onto acc l) (append (rev l) acc)))))))
; The theorem directly by induction, without the lemma: the hypothesis is
; about the empty accumulator, while rev-onto nil (cons x xs) computes to
; rev-onto (cons x nil) xs, so the last step is not a computation.
!(pf:proves &self
   (pf:induction list-ind (lam l (eq list (rev l) (rev-onto nil l)))
     (pf:refl list nil)
     (pf:fix x (pf:fix xs (pf:assume ih
       (pf:calc list (rev (cons x xs))
         (= (append (rev-onto nil xs) (cons x nil))
            (cong list (lam r (append r (cons x nil))) (rev xs) (rev-onto nil xs) ih))
         (= (rev-onto nil (cons x xs)) (pf:refl list (rev-onto nil (cons x xs)))))))))
   (all list (lam l (eq list (rev l) (rev-onto nil l)))))

; ---- The wrong reversal is the identity.  wrong-rev (cons a as) computes
; to cons a (wrong-rev as), and the hypothesis is carried under "a before".
!(pf:theorem &self wrong-rev-identity (all list (lam l (eq list (wrong-rev l) l)))
   (pf:induction list-ind (lam l (eq list (wrong-rev l) l))
     (pf:refl list nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong list list (lam r (cons a r)) (wrong-rev as) as ih))))))
; So it is not rev.  Two predicates into prop tell a list that starts with
; zero; wrong-rev keeps [0, 1] and rev turns it into [1, 0], so substitution
; along "wrong-rev [0, 1] = rev [0, 1]" turns that truth into Falsum.
!(set:define &self is-zero (-> num prop)
   (= (is-zero zero) (all prop (lam p (imp p p))))
   (= (is-zero (suc $n)) Falsum))
!(set:define &self starts-with-zero (-> list prop)
   (= (starts-with-zero nil) Falsum)
   (= (starts-with-zero (cons $a $as)) (is-zero $a)))
!(pf:theorem &self wrong-rev-refuted
   (imp (all list (lam l (eq list (wrong-rev l) (rev l)))) Falsum)
   (pf:assume H
     (pf:subst-with list starts-with-zero
       (wrong-rev (cons zero (cons (suc zero) nil)))
       (rev (cons zero (cons (suc zero) nil)))
       (pf:all-elim H (cons zero (cons (suc zero) nil)))
       (pf:fix p (pf:assume x x)))))
; The same argument does not refute the true theorem: rev-onto nil also
; turns [0, 1] into [1, 0].
!(pf:proves &self
   (pf:assume H
     (pf:subst-with list starts-with-zero
       (rev-onto nil (cons zero (cons (suc zero) nil)))
       (rev (cons zero (cons (suc zero) nil)))
       (pf:all-elim H (cons zero (cons (suc zero) nil)))
       (pf:fix p (pf:assume x x))))
   (imp (all list (lam l (eq list (rev-onto nil l) (rev l)))) Falsum))
; Nor does it refute the wrong reversal at a list of one number, where the
; two agree.
!(pf:proves &self
   (pf:assume H
     (pf:subst-with list starts-with-zero
       (wrong-rev (cons zero nil)) (rev (cons zero nil))
       (pf:all-elim H (cons zero nil))
       (pf:fix p (pf:assume x x))))
   (imp (all list (lam l (eq list (wrong-rev l) (rev l)))) Falsum))

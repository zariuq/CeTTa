; Lists of numbers and their append, on the face of sets.  The set: language
; is higher-order logic over the type `set` (In, Empty, Union, Power, Sep,
; Repl, Eps_set, UnivOf).  A declared datatype adds its type, constructors
; and induction principle to it, and a definition adds its equations, which
; the checker uses as computation.  Here "append l nil = l" is stated and
; proved by induction on lists; an invalid proof and a false variant are not
; proved, and the false variant is refuted; and the last part shows where
; lists and sets meet.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))
; The induction principle the declaration of the lists gives.
!(set:known-proposition &self list-ind)
; Equality on lists: reflexivity and substitution.  Congruence is derived.
!(pf:equality &self list refl@list subst@list)

; The theorem.  At the empty list `append nil nil` computes to nil; at a
; longer list `append (cons a as) nil` computes to `cons a (append as nil)`,
; and the hypothesis for `as` is carried under "a before".
!(pf:theorem &self append-nil (all list (lam l (eq list (append l nil) l)))
   (pf:induction list-ind (lam l (eq list (append l nil) l))
     (pf:refl list nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong list list (lam r (cons a r)) (append as nil) as ih))))))
!(set:known-proposition &self append-nil)
; The theorem used at one list.
!(pf:proves &self (pf:by append-nil)
   (eq list (append (cons zero nil) nil) (cons zero nil)))

; An invalid proof of the true theorem: a step that returns the hypothesis
; proves the statement for the rest of the list, not for the longer list.
!(pf:proves &self
   (pf:induction list-ind (lam l (eq list (append l nil) l))
     (pf:refl list nil)
     (pf:fix a (pf:fix as (pf:assume ih ih))))
   (all list (lam l (eq list (append l nil) l))))
; A false variant, "append l l = l".  A proof of the same shape does not
; prove it,
!(pf:proves &self
   (pf:induction list-ind (lam l (eq list (append l l) l))
     (pf:refl list nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong list list (lam r (cons a r)) (append as nil) as ih)))))
   (all list (lam l (eq list (append l l) l))))
; and it is refuted.  Two predicates on lists, defined by recursion into
; prop, tell a list whose tail is not empty; at the list [0], substitution
; along "[0] ++ [0] = [0]" turns that truth into Falsum.
!(set:define &self is-cons (-> list prop)
   (= (is-cons nil) Falsum)
   (= (is-cons (cons $a $as)) (all prop (lam p (imp p p)))))
!(set:define &self tail-cons (-> list prop)
   (= (tail-cons nil) Falsum)
   (= (tail-cons (cons $a $as)) (is-cons $as)))
!(pf:theorem &self append-self-refuted
   (imp (all list (lam l (eq list (append l l) l))) Falsum)
   (pf:assume H
     (pf:subst-with list tail-cons (append (cons zero nil) (cons zero nil)) (cons zero nil)
       (pf:all-elim H (cons zero nil))
       (pf:fix p (pf:assume x x)))))
; The same argument does not refute the true theorem: at [0] the predicate
; is already Falsum before the substitution.
!(pf:proves &self
   (pf:assume H
     (pf:subst-with list tail-cons (append (cons zero nil) nil) (cons zero nil)
       (pf:all-elim H (cons zero nil))
       (pf:fix p (pf:assume x x))))
   (imp (all list (lam l (eq list (append l nil) l))) Falsum))

; Lists and sets.  `set` is the sort of all sets, above every universe, and
; `list`, a type of the lowest universe, is one of them.  A function from
; lists to sets is a formed type;
!(set:formed &self (-> list set))
; but a list is not a set, so `In` does not apply to a list, while the type
; of lists is a set.
!(set:check &self (lam l (In l Empty)) (-> list prop))
!(set:check &self list set)
; A statement about sets that mentions lists needs a map from lists to sets,
; written by recursion.  This one sends a list to the power set of Empty
; iterated as many times as the list is long (it forgets the numbers).
!(set:define &self code (-> list set)
   (= (code nil) Empty)
   (= (code (cons $a $as)) (Power (code $as))))
; The laws of the universe give `x ∈ UnivOf x` (universeIn) and the closure
; of a universe under Union, Power and Repl (universeClosed).  Closure under
; the power set is the second of those three conditions, taken out of the
; conjunction, and with it every list's code lies in the universe of Empty,
; by induction on lists.
!(pf:theorem &self univ-power-closed
   (all set (lam N (all set (lam X (imp (In X (UnivOf N)) (In (Power X) (UnivOf N)))))))
   (pf:fix N (pf:by (pf:all-elim (pf:known universeClosed) N)
     (pf:assume union-closed (pf:assume rest
       (pf:by rest (pf:assume power-closed (pf:assume replacement-closed power-closed))))))))
!(pf:theorem &self codes-in-universe (all list (lam l (In (code l) (UnivOf Empty))))
   (pf:induction list-ind (lam l (In (code l) (UnivOf Empty)))
     (pf:all-elim (pf:known universeIn) Empty)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known univ-power-closed) Empty) (code as))
         ih))))))
; Without that closure the step has nothing to stand on.
!(pf:proves &self
   (pf:induction list-ind (lam l (In (code l) (UnivOf Empty)))
     (pf:all-elim (pf:known universeIn) Empty)
     (pf:fix a (pf:fix as (pf:assume ih ih))))
   (all list (lam l (In (code l) (UnivOf Empty)))))

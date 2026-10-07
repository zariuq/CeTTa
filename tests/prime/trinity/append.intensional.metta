(set:profile hol)

; Lists of numbers and their append, typed.  The two datatypes and append are
; declared in &self.  The kernel checks each declaration before it admits it:
; each right side must have the declared result type, and each recursive call
; must be on a smaller list.  Then the type: language says what is built and
; checked: the type of each constant and application (type:of, type:check),
; which terms are equal by computation (type:eq), and the theorem
; "append l nil = l" as a typed proof, a term of an identity type.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))

; ---- What each constant and each application is.
; The lists are a type of the lowest universe, and cons takes a number and a
; list.  A type of two arguments is read as a function that returns a
; function.
!(type:of &self list)
!(type:of &self cons)
!(type:of &self append)
!(type:check &self append (-> list list list))
!(type:check &self append (-> list list))
; append of two lists is a list, and not a number; append of one list is a
; function from lists to lists.
!(type:of &self (append (cons zero nil) (cons (suc zero) nil)))
!(type:check &self (append (cons zero nil) (cons (suc zero) nil)) list)
!(type:check &self (append (cons zero nil) (cons (suc zero) nil)) num)
!(type:of &self (append (cons zero nil)))
; A number is not a list: the application has no type.
!(try (type:of &self (append zero nil)))
!(type:check &self (append zero nil) list)

; ---- What runs is typed.
; Each equation is an equality of the judgment at closed arguments,
!(type:eq &self (append nil (cons zero nil)) (cons zero nil))
!(type:eq &self (append (cons zero nil) (cons (suc zero) nil))
   (cons zero (append nil (cons (suc zero) nil))))
; and so is a run of several steps: an expression equals its value,
!(type:eq &self (append (cons zero nil) (cons (suc zero) nil))
   (cons zero (cons (suc zero) nil)))
; and two expressions with one value are equal, however they are nested.
!(type:eq &self (append (append (cons zero nil) nil) (cons (suc zero) nil))
   (append (cons zero nil) (append nil (cons (suc zero) nil))))
; Different values are not equal.
!(type:eq &self (cons zero nil) (cons (suc zero) nil))
!(type:eq &self (append (cons zero nil) (cons (suc zero) nil))
   (cons (suc zero) (cons zero nil)))
; Both equations hold at open arguments, under binders: the two sides
; compute to one term.
!(type:eq &self (lam (ys : list) (append nil ys)) (lam (ys : list) ys))
!(type:eq &self
   (lam (a : num) (lam (as : list) (lam (ys : list) (append (cons a as) ys))))
   (lam (a : num) (lam (as : list) (lam (ys : list) (cons a (append as ys))))))
; At an unknown list, nil appended to it computes to it, but it appended to
; nil does not compute, because append looks at its first argument.  So
; "append l nil = l" is not a computation: it needs a proof.
!(add-atom &self (: some-list list))
!(type:eq &self (append nil some-list) some-list)
!(type:eq &self (append some-list nil) some-list)

; ---- The theorem "append l nil = l" as a typed proof.
; Congruence of identity proofs under "a number before", by the identity
; eliminator.  It is a term; nothing is added to the kernel for it.
!(set:define &self cong-cons
   (-> (a : num) (x : list) (y : list) (p : (id list x y))
       (id list (cons a x) (cons a y)))
   (= (cong-cons $a $x $y $p)
      (id:eliminate list $x
         (lam y2 (lam e (id list (cons $a $x) (cons $a y2))))
         (refl (cons $a $x)) $y $p)))
; The recursor of the lists: its type is the induction principle.
!(type:of &self list-rec)
; The proof is the recursor at the family of identity types: reflexivity at
; the empty list, since append nil nil computes to nil; at a longer list the
; congruence of the induction hypothesis, since append (cons a as) nil
; computes to cons a (append as nil).
!(type:check &self
   (lam (l : list)
     (list-rec (lam l2 (id list (append l2 nil) l2))
               (refl nil)
               (lam a (lam as (lam ih (cong-cons a (append as nil) as ih))))
               l))
   (-> (l : list) (id list (append l nil) l)))
; The same proof written as two equations: a definition by recursion on the
; list, whose recursive call stands where the induction hypothesis goes.  The
; kernel checks both right sides against the statement before it admits it.
!(set:define &self append-nil
   (-> (l : list) (id list (append l nil) l))
   (= (append-nil nil) (refl nil))
   (= (append-nil (cons $a $as))
      (cong-cons $a (append $as nil) $as (append-nil $as))))
!(type:check &self append-nil (-> (l : list) (id list (append l nil) l)))
; At a closed list it proves that instance, in both spellings, and it
; computes to reflexivity.
!(type:check &self (append-nil (cons zero nil))
   (id list (append (cons zero nil) nil) (cons zero nil)))
!(type:check &self (append-nil (cons zero nil))
   (id list (cons zero nil) (cons zero nil)))
!(type:eq &self (append-nil (cons zero nil)) (refl (cons zero nil)))
; At the unknown list it is a proof that does not compute; at a list that
; starts with a known number it takes one step, by its second equation.
!(type:check &self (append-nil some-list)
   (id list (append some-list nil) some-list))
!(type:eq &self (append-nil (cons zero some-list))
   (cong-cons zero (append some-list nil) some-list (append-nil some-list)))

; ---- Wrong proofs are refused.
; Reflexivity alone: at an unknown list the two sides are not equal by
; computation.
!(try (type:check &self (lam (l : list) (refl l))
        (-> (l : list) (id list (append l nil) l))))
!(type:check &self (refl some-list) (id list (append some-list nil) some-list))
; A step that returns the induction hypothesis unchanged proves the statement
; for the rest, not for the longer list: the second right side is refused.
!(try (set:define &self wrong-step
   (-> (l : list) (id list (append l nil) l))
   (= (wrong-step nil) (refl nil))
   (= (wrong-step (cons $a $as)) (wrong-step $as))))
; A base at another list.
!(let $r (try (set:define &self wrong-base
   (-> (l : list) (id list (append l nil) l))
   (= (wrong-base nil) (refl (cons zero nil)))
   (= (wrong-base (cons $a $as))
      (cong-cons $a (append $as nil) $as (wrong-base $as)))))
   (car-atom $r))
; A false statement: no list is the list with zero before it.
!(let $r (try (set:define &self wrong-statement
   (-> (l : list) (id list (cons zero l) l))
   (= (wrong-statement nil) (refl nil))
   (= (wrong-statement (cons $a $as))
      (cong-cons $a (cons zero $as) $as (wrong-statement $as)))))
   (car-atom $r))
; The proof does not prove a false instance: the empty list is not the list
; of zero.
!(type:check &self (append-nil (cons zero nil)) (id list nil (cons zero nil)))

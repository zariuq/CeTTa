(set:profile hol)

; Lists over a type and their append, typed.  The lists are declared at the
; lowest universe with a parameter in it, so that a list of any type of
; that universe is a type of it again, and lists have identity types.  The
; kernel checks the declaration before admitting it: each constructor's
; fields lie in the universe, with the parameter standing for any type of
; it.  append, and the theorem "append l nil = l" as a typed proof, are
; written once, for every A.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self (List (: A (u 0))) (u 0)
   (: nil (List A))
   (: cons (-> A (List A) (List A))))
!(set:define &self append (-> (A : (u 0)) (l m : (List A)) (List A))
   (= (append $A (nil $A) $m) $m)
   (= (append $A (cons $A $a $l) $m) (cons $A $a (append $A $l $m))))

; ---- What each constant and each application is.
; List takes a type to a type; nil, cons and append take the type first.
!(type:of &self List)
!(type:of &self nil)
!(type:of &self cons)
!(type:check &self append (-> (A : (u 0)) (l m : (List A)) (List A)))
; The recursor takes the type, then the motive, the two methods and a list.
!(type:of &self List-rec)
; Applications with the parameters written,
!(type:of &self (cons num zero (nil num)))
!(type:of &self (append num (cons num zero (nil num)) (nil num)))
!(type:of &self (append (List num) (cons (List num) (nil num) (nil (List num))) (nil (List num))))
!(type:check &self (cons num zero (nil num)) (List num))
!(type:check &self (cons num zero (nil num)) (List (List num)))
; and left out: the parameters are found against the type a use is checked
; at, or from the type of a field.
!(type:check &self nil (List num))
!(type:check &self (cons zero nil) (List num))
!(type:check &self (cons nil nil) (List (List num)))
!(type:of &self (cons zero nil))
!(type:of &self (append num (cons zero nil) nil))
; A number in a list of lists is refused: the type checked at gives the
; parameter, so no other reading is left.
!(try (type:check &self (cons zero nil) (List (List num))))
; Here nothing gives the parameter of the first field, nil: any list type
; would do.  Not decided, and not refused.
!(try (type:of &self (cons nil nil)))
; List takes one argument, and nil one parameter.
!(try (type:formed &self (List num num)))
!(try (type:check &self (nil num num) (List num)))

; ---- Conversion.
; Two applications of List to convertible arguments are convertible.
!(set:define &self Nat (u 0) (= Nat num))
!(type:eq &self (List Nat) (List num))
!(type:eq &self (List (List Nat)) (List (List num)))
!(type:check &self (cons Nat zero (nil num)) (List num))
; Each equation of append is an equality of the judgment at closed
; arguments, at each instance, in both spellings,
!(type:eq &self (append num (nil num) (cons num zero (nil num))) (cons num zero (nil num)))
!(type:eq &self (append (List num) (cons (List num) (nil num) (nil (List num))) (nil (List num)))
                (cons (List num) (nil num) (nil (List num))))
!(type:eq &self (append num (cons zero nil) (cons (suc zero) nil)) (cons zero (cons (suc zero) nil)))
; and under binders, at every type: the nil equation computes there.
!(type:eq &self (lam (A : (u 0)) (lam (ys : (List A)) (append A (nil A) ys)))
                (lam (A : (u 0)) (lam (ys : (List A)) ys)))
; At an unknown list, nil appended to it computes to it, and it appended to
; nil does not compute: "append l nil = l" needs a proof.
!(add-atom &self (: some-list (List num)))
!(type:eq &self (append num (nil num) some-list) some-list)
!(type:eq &self (append num some-list (nil num)) some-list)
; Constructor heads stay distinct at every instance.  The refutation rests
; on the conversion algorithm being complete for the declarations it meets,
; which Lean has not proved for datatypes with parameters: the record says
; so.
!(try (type:eq &self (nil num) (cons num zero (nil num))))
!(try (type:eq &self (nil (List num)) (cons (List num) (nil num) (nil (List num)))))
!(try (type:eq &self nil (cons zero nil)))
!(try (type:eq &self (cons num zero (nil num)) (cons num (suc zero) (nil num))))
; The recursor computes on the constructors: the length of [0, 0].
!(type:eq &self
   (List-rec num (lam l num) zero (lam a (lam as (lam ih (suc ih))))
             (cons num zero (cons num zero (nil num))))
   (suc (suc zero)))

; ---- The theorem "append l nil = l" as a typed proof, for every A.
; Congruence of identity proofs under "a before", by the identity
; eliminator.
!(set:define &self cong-cons
   (-> (A : (u 0)) (a : A) (x : (List A)) (y : (List A)) (p : (id (List A) x y))
       (id (List A) (cons A a x) (cons A a y)))
   (= (cong-cons $A $a $x $y $p)
      (id:eliminate (List $A) $x
         (lam y2 (lam e (id (List $A) (cons $A $a $x) (cons $A $a y2))))
         (refl (cons $A $a $x)) $y $p)))
; The proof by recursion on the list: reflexivity at nil, the congruence of
; the hypothesis at cons.  The kernel checks both right sides against the
; statement before it admits the definition.
!(set:define &self append-nil
   (-> (A : (u 0)) (l : (List A)) (id (List A) (append A l (nil A)) l))
   (= (append-nil $A (nil $A)) (refl (nil $A)))
   (= (append-nil $A (cons $A $a $as))
      (cong-cons $A $a (append $A $as (nil $A)) $as (append-nil $A $as))))
; At a closed list of each instance it proves that instance, and computes
; to reflexivity.
!(type:check &self (append-nil num (cons num zero (nil num)))
   (id (List num) (append num (cons num zero (nil num)) (nil num)) (cons num zero (nil num))))
!(type:eq &self (append-nil num (cons num zero (nil num))) (refl (cons num zero (nil num))))
!(type:check &self (append-nil (List num) (cons (List num) (nil num) (nil (List num))))
   (id (List (List num))
       (append (List num) (cons (List num) (nil num) (nil (List num))) (nil (List num)))
       (cons (List num) (nil num) (nil (List num)))))
; The same proof as a term: the recursor at the family of identity types.
!(type:check &self
   (lam (A : (u 0)) (lam (l : (List A))
     (List-rec A (lam l2 (id (List A) (append A l2 (nil A)) l2))
               (refl (nil A))
               (lam a (lam as (lam ih (cong-cons A a (append A as (nil A)) as ih))))
               l)))
   (-> (A : (u 0)) (l : (List A)) (id (List A) (append A l (nil A)) l)))
; A step that returns the induction hypothesis unchanged proves the
; statement for the rest of the list, not for the longer list: refused.
!(try (set:define &self wrong-step
   (-> (A : (u 0)) (l : (List A)) (id (List A) (append A l (nil A)) l))
   (= (wrong-step $A (nil $A)) (refl (nil $A)))
   (= (wrong-step $A (cons $A $a $as)) (wrong-step $A $as))))
; The proof does not prove a false instance.
!(type:check &self (append-nil num (cons num zero (nil num)))
   (id (List num) (nil num) (cons num zero (nil num))))

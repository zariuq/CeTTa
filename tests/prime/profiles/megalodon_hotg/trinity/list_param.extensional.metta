(set:profile megalodon-hotg)

; Lists over a set, on the face of sets.  The set: language is
; higher-order logic over the type `set`, and a declared datatype adds its
; type, its constructors and its induction principle to it.  Declared with
; a parameter, the principle is a schema over the parameter: `List-ind` is
; cited at a set A as `(List-ind A)`.  Here "append l nil = l" is proved by
; induction at the lists of numbers, with the parameters written, and at
; the lists of lists of numbers, with them left out; an invalid proof and a
; false variant are not proved, and the false variant is refuted.  The last
; part shows where lists meet sets, and which Lean theorem gives the
; declaration a set model.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self (List (: A set)) set
   (: nil (List A))
   (: cons (-> A (List A) (List A))))
!(set:define &self append (-> (A : set) (l m : (List A)) (List A))
   (= (append $A (nil $A) $m) $m)
   (= (append $A (cons $A $a $l) $m) (cons $A $a (append $A $l $m))))
; The declaration as recorded: the type of List with its parameter bound,
; then the constructors as written.
!(set:known-proposition &self List)
; The induction principle the declaration gives, a schema over A.
!(set:known-proposition &self List-ind)

; ---- At the lists of numbers, the parameters written.
!(pf:equality &self (List num) refl@ln subst@ln)
; At the empty list `append num (nil num) (nil num)` computes to nil; at a
; longer list `append num (cons num a as) (nil num)` computes to
; `cons num a (append num as (nil num))`, and the hypothesis for `as` is
; carried under "a before".
!(pf:theorem &self append-nil-num
   (all (List num) (lam l (eq (List num) (append num l (nil num)) l)))
   (pf:induction (List-ind num) (lam l (eq (List num) (append num l (nil num)) l))
     (pf:refl (List num) (nil num))
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong (List num) (List num) (lam r (cons num a r))
                (append num as (nil num)) as ih))))))
!(set:known-proposition &self append-nil-num)
; The theorem used at one list.
!(pf:proves &self (pf:by append-nil-num)
   (eq (List num) (append num (cons num zero (nil num)) (nil num)) (cons num zero (nil num))))

; ---- At the lists of lists of numbers, the parameters left out.  The
; statement and the proof are read as with them written: each list at the
; type its place gives it.
!(pf:equality &self (List (List num)) refl@lln subst@lln)
!(pf:theorem &self append-nil-lists
   (all (List (List num)) (lam l (eq (List (List num)) (append (List num) l nil) l)))
   (pf:induction (List-ind (List num)) (lam l (eq (List (List num)) (append (List num) l nil) l))
     (pf:refl (List (List num)) nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong (List (List num)) (List (List num)) (lam r (cons a r))
                (append (List num) as nil) as ih))))))
!(pf:proves &self (pf:by append-nil-lists)
   (eq (List (List num)) (append (List num) (cons nil nil) nil) (cons nil nil)))

; ---- Invalid proofs and a false variant.
; A step that returns the hypothesis proves the statement for the rest of
; the list, not for the longer list.
!(pf:proves &self
   (pf:induction (List-ind num) (lam l (eq (List num) (append num l (nil num)) l))
     (pf:refl (List num) (nil num))
     (pf:fix a (pf:fix as (pf:assume ih ih))))
   (all (List num) (lam l (eq (List num) (append num l (nil num)) l))))
; The principle at the lists of numbers proves nothing of the lists of
; lists.
!(pf:proves &self
   (pf:induction (List-ind num) (lam l (eq (List (List num)) (append (List num) l nil) l))
     (pf:refl (List (List num)) nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong (List (List num)) (List (List num)) (lam r (cons a r))
                (append (List num) as nil) as ih)))))
   (all (List (List num)) (lam l (eq (List (List num)) (append (List num) l nil) l))))
; A false variant, "append l l = l", is not proved by a proof of the same
; shape,
!(pf:proves &self
   (pf:induction (List-ind num) (lam l (eq (List num) (append num l l) l))
     (pf:refl (List num) (nil num))
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong (List num) (List num) (lam r (cons num a r))
                (append num as (nil num)) as ih)))))
   (all (List num) (lam l (eq (List num) (append num l l) l))))
; and it is refuted.  Two predicates on lists of numbers, defined by
; recursion into prop, tell a list whose tail is not empty; at the list
; [0], substitution along "[0] ++ [0] = [0]" turns that truth into Falsum.
!(set:define &self is-cons (-> (l : (List num)) prop)
   (= (is-cons (nil num)) Falsum)
   (= (is-cons (cons num $a $as)) (all prop (lam p (imp p p)))))
!(set:define &self tail-cons (-> (l : (List num)) prop)
   (= (tail-cons nil) Falsum)
   (= (tail-cons (cons $a $as)) (is-cons $as)))
!(pf:theorem &self append-self-refuted
   (imp (all (List num) (lam l (eq (List num) (append num l l) l))) Falsum)
   (pf:assume H
     (pf:subst-with (List num) tail-cons
       (append num (cons zero nil) (cons zero nil)) (cons zero nil)
       (pf:all-elim H (cons zero nil))
       (pf:fix p (pf:assume x x)))))
; The principle is cited at as many sets as List has parameters.
!(try (set:proves &self (pf:all-elim (pf:known (List-ind num num)) (lam l Falsum))
        (imp Falsum Falsum)))
; At a set whose name the principle also writes as a bound name, `v1`, the
; bound name is renamed first: the instance is the principle at that set,
; and the same proof proves the theorem there.
!(set:inductive &self v1 (u 0) (: one v1))
!(pf:equality &self (List v1) refl@lv subst@lv)
!(pf:theorem &self append-nil-v1
   (all (List v1) (lam l (eq (List v1) (append v1 l nil) l)))
   (pf:induction (List-ind v1) (lam l (eq (List v1) (append v1 l nil) l))
     (pf:refl (List v1) nil)
     (pf:fix a (pf:fix as (pf:assume ih
       (pf:cong (List v1) (List v1) (lam r (cons a r)) (append v1 as nil) as ih))))))

; ---- Equality of lists, at each instance.
!(set:eq &self (append num (cons num zero (nil num)) (nil num)) (cons num zero (nil num)))
!(set:eq &self (append (List num) (cons nil nil) nil) (cons nil nil))
!(set:eq &self (append num (cons zero nil) nil) (cons (suc zero) nil))

; ---- Lists and sets.  The lists of a set are a set, and so are the lists
; of those lists; a function from lists to sets is a formed type.
!(set:check &self (List num) set)
!(set:check &self (List (List num)) set)
!(set:formed &self (-> (List num) set))
; A list is not a set: `In` does not apply to a list.
!(set:check &self (lam l (In l Empty)) (-> (List num) prop))
; What a list is, as the set language reads it: written either way.
!(set:of &self (cons num zero (nil num)))
!(set:of &self (cons (cons zero nil) nil))

; ---- The set model.  Lean declares the lists over a set as a family over
; the tower inside the sets (List : set -> set, nil, cons and append) and
; gives it a set model: List A is the set of finite lists of members of A.
; A declaration of the lists written as Lean writes that family, up to the
; name of its parameter, has its record name that model, with the
; hypotheses it holds under; the model is of List, nil and cons, and states
; nothing of the recursor or of the principle.
!(set:known-proof &self List)
!(bind! &other (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(set:inductive &other (List (: B set)) set
   (: nil (List B))
   (: cons (-> B (List B) (List B))))
!(set:known-proof &other List)
; Any other declaration with parameters cites no Lean model: a tree,
!(set:inductive &other (Tree (: A set)) set
   (: leaf (Tree A))
   (: node (-> (Tree A) A (Tree A) (Tree A))))
!(set:known-proof &other Tree)
; and the lists over a type of the lowest universe, a datatype of that
; universe, which is not Lean's family.
!(bind! &small (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(set:inductive &small (List (: A (u 0))) (u 0)
   (: nil (List A))
   (: cons (-> A (List A) (List A))))
!(set:known-proof &small List)
; The lists over any set cannot all lie in the lowest universe: the field
; of cons at a set A is not of that universe, and the declaration is
; refuted.  (Lean proves the same of the list family with List : set -> U0
; when List is read as the lists: it has no set model.)
!(bind! &wrong (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(let $r (try (set:inductive &wrong (List (: A set)) (u 0)
                (: nil (List A))
                (: cons (-> A (List A) (List A)))))
   (car-atom $r))

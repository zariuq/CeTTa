; A proof by induction about a declared datatype: every list of numbers
; appended to the empty list is identical to the list.  The same proof is a
; derivation of the Lean development (appendNil_identity, in the object
; package with the lists of numbers); each comment names the Lean theorem
; that licenses the verdict.

!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
; The type of the constructor of longer lists is written with nested arrows:
; (-> num (-> list list)) is the type (-> num list list), and the declaration
; is admitted with the same induction principle and recursor.
!(set:inductive &s list (u 0) (: nil list) (: cons (-> num (-> list list))))
!(type:of &s list-rec)
!(bind! &flat (new-space))
!(set:inductive &flat num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &flat list (u 0) (: nil list) (: cons (-> num list list)))
!(== (type:of &s list-rec) (type:of &flat list-rec))

; A program by the recursor: a list appended to the empty list.
!(set:define &s append-nil (-> list list)
   (= (append-nil $l)
      (list-rec (lam t list) nil (lam a (lam as (lam r (cons a r)))) $l)))
; Its two computation rules at closed lists (appendNil_nil, appendNil_cons),
; and a comparison that is false.
!(type:eq &s (append-nil nil) nil)
!(type:eq &s (append-nil (cons zero nil)) (cons zero (append-nil nil)))
!(type:eq &s (append-nil (cons zero nil)) (cons zero nil))
!(type:eq &s (append-nil (cons zero nil)) nil)

; Congruence of identity proofs under "the number before", by the identity
; eliminator (cong_typed).  Nothing is added to the kernel: it is a term.
!(set:define &s cong-cons
   (-> (a : num) (x : list) (y : list) (p : (id list x y))
       (id list (cons a x) (cons a y)))
   (= (cong-cons $a $x $y $p)
      (id:eliminate list $x
         (lam y2 (lam e (id list (cons $a $x) (cons $a y2))))
         (refl (cons $a $x)) $y $p)))
!(type:check &s (cong-cons zero nil nil (refl nil))
   (id list (cons zero nil) (cons zero nil)))

; The proof by induction (appendNil_identity): the recursor at the family of
; identity types, reflexivity at the empty list, and at a longer list the
; congruence of the induction hypothesis.  The kernel checks the definition
; against its declared type before it admits it.
!(set:define &s append-nil-id
   (-> (l : list) (id list (append-nil l) l))
   (= (append-nil-id $l)
      (list-rec (lam l2 (id list (append-nil l2) l2))
                (refl nil)
                (lam a (lam as (lam ih (cong-cons a (append-nil as) as ih))))
                $l)))
!(type:check &s append-nil-id (-> (l : list) (id list (append-nil l) l)))
; At the one-element list (appendNil_identity_one), at both spellings of the
; identity type; at a closed list the proof computes to reflexivity.
!(type:check &s (append-nil-id (cons zero nil))
   (id list (append-nil (cons zero nil)) (cons zero nil)))
!(type:check &s (append-nil-id (cons zero nil))
   (id list (cons zero nil) (cons zero nil)))
!(type:eq &s (append-nil-id nil) (refl nil))
!(type:eq &s (append-nil-id (cons zero nil)) (refl (cons zero nil)))

; Wrong proofs are not admitted.  A step that returns the induction
; hypothesis unchanged proves the statement for the rest, not for the longer
; list; a base at another list; a statement that is false.
!(let $r (try (set:define &s wrong-step
   (-> (l : list) (id list (append-nil l) l))
   (= (wrong-step $l)
      (list-rec (lam l2 (id list (append-nil l2) l2))
                (refl nil)
                (lam a (lam as (lam ih ih)))
                $l))))
   (car-atom $r))
!(let $r (try (set:define &s wrong-base
   (-> (l : list) (id list (append-nil l) l))
   (= (wrong-base $l)
      (list-rec (lam l2 (id list (append-nil l2) l2))
                (refl (cons zero nil))
                (lam a (lam as (lam ih (cong-cons a (append-nil as) as ih))))
                $l))))
   (car-atom $r))
!(let $r (try (set:define &s wrong-statement
   (-> (l : list) (id list (cons zero l) l))
   (= (wrong-statement $l)
      (list-rec (lam l2 (id list (cons zero l2) l2))
                (refl nil)
                (lam a (lam as (lam ih (cong-cons a (cons zero as) as ih))))
                $l))))
   (car-atom $r))
; Reflexivity alone is no proof: at a variable list, appending does not
; compute, so induction is needed.
!(try (type:check &s (lam (l : list) (refl l))
        (-> (l : list) (id list (append-nil l) l))))
; The empty list is not the one-element list (nil_not_identical_to_one):
; reflexivity is no proof of it, and the proof by induction does not give one.
!(type:check &s (refl nil) (id list nil (cons zero nil)))
!(type:check &s (append-nil-id (cons zero nil)) (id list nil (cons zero nil)))

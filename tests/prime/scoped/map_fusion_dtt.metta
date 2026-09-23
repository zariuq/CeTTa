; Map fusion as a computing dependent proof.  The list type is declared by
; its constructors; the declaration derives the induction principle and the
; recursor with its computation rules.  The functions are declared by their
; equations, which the kernel computes with, so the theorem's two sides meet
; by conversion on constructors and the step case is one application of the
; identity eliminator to the hypothesis.  The proof is a term of the
; declared type, checked by the type: language against the theory space.
!(bind! &thy (new-space))
!(add-atom &thy (: elem (u 0)))
!(add-atom &thy (: f (-> elem elem)))
!(add-atom &thy (: g (-> elem elem)))
!(add-atom &thy (: a elem))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(set:define &thy mapf (-> (-> elem elem) list list)
   (= (mapf $h nil) nil)
   (= (mapf $h (cons $x $xs)) (cons ($h $x) (mapf $h $xs))))
!(set:define &thy comp (-> (-> elem elem) (-> elem elem) elem elem)
   (= (comp $p $q $x) ($p ($q $x))))
; what the declaration derived
!(collapse (match &thy (type:rule $h $n $p $r) $h))
; the kernel computes on constructors with the declared equations
!(type:eq &thy (mapf f (mapf g (cons a nil))) (mapf (comp f g) (cons a nil)))
!(type:eq &thy (mapf f (mapf g (cons a nil))) (cons (g (f a)) nil))
; the theorem, as a dependent proof by the recursor
!(type:check &thy
   (lam xs (list-rec (lam l (id list (mapf f (mapf g l)) (mapf (comp f g) l)))
     (refl nil)
     (lam x (lam l (lam ih
       (id:eliminate list (mapf f (mapf g l))
         (lam y (lam e (id list (cons (f (g x)) (mapf f (mapf g l))) (cons (f (g x)) y))))
         (refl (cons (f (g x)) (mapf f (mapf g l))))
         (mapf (comp f g) l) ih))))
     xs))
   (-> (xs : list) (id list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
; the proof instantiated at a concrete list, by substitution, has the
; concrete computed type: the recursor runs on the constructors
!(type:check &thy
   (list-rec (lam l (id list (mapf f (mapf g l)) (mapf (comp f g) l)))
     (refl nil)
     (lam x (lam l (lam ih
       (id:eliminate list (mapf f (mapf g l))
         (lam y (lam e (id list (cons (f (g x)) (mapf f (mapf g l))) (cons (f (g x)) y))))
         (refl (cons (f (g x)) (mapf f (mapf g l))))
         (mapf (comp f g) l) ih))))
     (cons a nil))
   (id list (cons (f (g a)) nil) (cons (f (g a)) nil)))
; the same proof does not establish the composition in the other order
!(type:check &thy
   (lam xs (list-rec (lam l (id list (mapf f (mapf g l)) (mapf (comp g f) l)))
     (refl nil)
     (lam x (lam l (lam ih
       (id:eliminate list (mapf f (mapf g l))
         (lam y (lam e (id list (cons (f (g x)) (mapf f (mapf g l))) (cons (f (g x)) y))))
         (refl (cons (f (g x)) (mapf f (mapf g l))))
         (mapf (comp g f) l) ih))))
     xs))
   (-> (xs : list) (id list (mapf f (mapf g xs)) (mapf (comp g f) xs))))
; reflexivity alone does not prove it: on a variable, nothing computes
!(type:check &thy (lam xs (refl (mapf f (mapf g xs))))
   (-> (xs : list) (id list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
; the computation rules are facts of the theory space: with the same
; constants declared but no equations, the base case no longer converts
!(bind! &bare (new-space))
!(add-atom &bare (: elem (u 0)))
!(add-atom &bare (: f (-> elem elem)))
!(add-atom &bare (: g (-> elem elem)))
!(set:inductive &bare list (u 0) (: nil list) (: cons (-> elem list list)))
!(add-atom &bare (: mapf (-> (-> elem elem) list list)))
!(add-atom &bare (: comp (-> (-> elem elem) (-> elem elem) elem elem)))
!(type:check &bare (refl nil) (id list (mapf f (mapf g nil)) (mapf (comp f g) nil)))
!(type:check &thy (refl nil) (id list (mapf f (mapf g nil)) (mapf (comp f g) nil)))

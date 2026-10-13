; Datatype typing needs no HOTG model; a concrete set model needs its profile.
!(bind! &logic (new-space))
!(bind! &hotg (new-space))
!(add-atom &logic (hol:profile base))
!(add-atom &hotg (set:profile megalodon-hotg))
!(hol:inductive &logic (List (: A set)) set
   (: nil (List A)) (: cons (-> A (List A) (List A))))
!(hol:known-proof &logic List)
!(hol:formed &logic (-> (List prop) (List prop)))
!(set:inductive &hotg (List (: A set)) set
   (: nil (List A)) (: cons (-> A (List A) (List A))))
!(set:known-proof &hotg List)

; The same stream family cannot publish a model-derived law in the logic alone.
!(let $result (try (hol:family &logic
   (: Stream (-> set set))
   (: scons (-> (A : set) (a : A) (s : (Stream A)) (Stream A)))
   (: shead (-> (A : set) (s : (Stream A)) A))
   (: stail (-> (A : set) (s : (Stream A)) (Stream A)))
   (: iter (-> (A : set) (f : (-> A A)) (a : A) (Stream A)))
   (= (shead $A (scons $A $a $s)) $a)
   (= (stail $A (scons $A $a $s)) $s)
   (= (iter $A $f $a) (scons $A $a (iter $A $f ($f $a))))))
   (car-atom $result))
!(match &logic (hol:known Stream $proposition family $evidence $depends $revision $signature)
   unexpected-model)

; Explicit HOTG interpretation preserves the same family and its conditional model.
!(set:family &hotg
   (: Stream (-> set set))
   (: scons (-> (A : set) (a : A) (s : (Stream A)) (Stream A)))
   (: shead (-> (A : set) (s : (Stream A)) A))
   (: stail (-> (A : set) (s : (Stream A)) (Stream A)))
   (: iter (-> (A : set) (f : (-> A A)) (a : A) (Stream A)))
   (= (shead $A (scons $A $a $s)) $a)
   (= (stail $A (scons $A $a $s)) $s)
   (= (iter $A $f $a) (scons $A $a (iter $A $f ($f $a)))))
!(match &hotg (set:known Stream $proposition family $evidence $depends $revision $signature)
   preserved-model)

!(import! &self declaration-builders.metta)

; A lambda calls an ordinary MeTTa constructor program. Its output is the
; declaration value, whose represented types have their own binders.
!(let $built ((lam family (decl:nat family Zero Succ)) Nat)
   (assertEqual (decl:read-family $built)
     (decl:read-block (quote (Declarations
       (: (Family Nat (parameters ()) (indices ())) Type)
       (= Nat (data ((: (Zero (fields ())) Nat)
                    (: (Succ (fields ((: previous Nat)))) Nat)))))))))

!(let $built ((lam family (decl:vec family Nat Zero Succ)) Vec)
   (assertEqual (decl:read-family $built)
     (decl:read-block (quote (Declarations
       (: (Family Vec (parameters ((: A Type))) (indices ((: length Nat)))) Type)
       (= Vec (data
         ((: (Nil (fields ())) ((Vec A) Zero))
          (: (Cons (fields ((: n Nat) (: head A) (: tail ((Vec A) n)))))
             ((Vec A) (Succ n)))))))))))

; Inspection retains each component, including the dependent result index.
!(let (Some (quote (Family $name (parameters $parameters) (indices $indices) $sort
                  (constructors $constructors))))
        (decl:read-family (decl:vec Vec Nat Zero Succ))
   (assertEqual (quote ($name $parameters $indices $sort $constructors))
     (quote (Vec ((: A Type)) ((: length Nat)) Type
       ((: (Nil (fields ())) ((Vec A) Zero))
        (: (Cons (fields ((: n Nat) (: head A) (: tail ((Vec A) n)))))
           ((Vec A) (Succ n))))))))

!(assertEqual (decl:read-block (quote (Declarations
   (: (Family Nat (parameters ()) (indices ())) Type)))) None)
!(assertEqual (decl:read-block (quote (Declarations
   (: (Family Nat (parameters ()) (indices ())) Type)
   (= Wrong (data ()))))) None)
!(assertEqual (decl:read-block (quote (Declarations
   (: (Family Nat (parameters ()) (indices ())) Type)
   (= Nat (data ())) Extra))) None)
!(assertEqual (decl:read-family
   (decl:family Broken (quote ((: unused))) (quote ()) (quote Type) (quote ()))) None)
!(assertEqual (decl:read-family
   (decl:family Broken (quote ()) (quote ()) (quote Type)
     (quote ((: (Bad (fields ((: x Nat))))))))) None)

; Source parsing preserves a proposed result. It does not approve its index.
!(assertEqual (=alpha
   (decl:vec Vec Nat Zero Succ)
   (decl:family Vec (quote ((: A Type))) (quote ((: length Nat))) (quote Type)
     (quote ((: (Nil (fields ())) ((Vec A) Zero))
             (: (Cons (fields ((: n Nat) (: head A) (: tail ((Vec A) n)))))
                ((Vec A) n)))))) False)

; Quoted type syntax must never run during inspection.
!(bind! &inspection (new-space))
(= (touch $x) (add-atom &inspection touched))
!(assertEqual (decl:term-shape (quote (touch payload))) True)
!(assertEqual (collapse (match &inspection touched touched)) ())
!(assertEqual (decl:read-family
   (decl:family Broken (quote not-a-telescope) (quote ()) (quote Type) (quote ()))) None)
!(assertEqual (decl:read-family
   (decl:family Broken (quote ()) (quote ()) (quote Type) (quote not-a-constructor-list))) None)

!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (parameter-order:duplicates) (HOL:Cons "B" (HOL:Cons "A" (HOL:Cons "A" HOL:Nil))))
(= (parameter-order:term)
  (HOL:Lam (Tyvar "A") (HOL:Lam (Tyvar "B") (HOL:Bound 1 (Tyvar "A")))))
(= (parameter-order:parameters)
  (hol-light:type-parameters (hol-light:sorted-names (hol-light:term-type-vars (parameter-order:term)))))

!(ParameterOrderControl empty (== (hol-light:sorted-names HOL:Nil) HOL:Nil))
!(ParameterOrderControl roundtrip
  (== (hol-light:tagged-list (hol-light:native-list (parameter-order:duplicates))) (parameter-order:duplicates)))
!(ParameterOrderControl duplicates
  (== (hol-light:sorted-names (parameter-order:duplicates))
      (HOL:Cons "A" (HOL:Cons "A" (HOL:Cons "B" HOL:Nil)))))
!(ParameterOrderControl not-deduplication
  (== (== (hol-light:sorted-names (parameter-order:duplicates))
          (HOL:Cons "A" (HOL:Cons "B" HOL:Nil))) False))
!(ParameterOrderControl lexical
  (== (hol-light:sorted-names (HOL:Cons "a" (HOL:Cons "B" (HOL:Cons "A2"
           (HOL:Cons "A10" (HOL:Cons "A" HOL:Nil))))))
      (HOL:Cons "A" (HOL:Cons "A10" (HOL:Cons "A2" (HOL:Cons "B" (HOL:Cons "a" HOL:Nil)))))))
!(ParameterOrderControl unicode
  (== (hol-light:sorted-names (HOL:Cons "𐀀" (HOL:Cons "あ" (HOL:Cons "é" (HOL:Cons "A" HOL:Nil)))))
      (HOL:Cons "A" (HOL:Cons "é" (HOL:Cons "あ" (HOL:Cons "𐀀" HOL:Nil))))))
!(ParameterOrderControl input-type
  (== (hol-light:term-type (parameter-order:term))
      (Some (hol-light:fun (Tyvar "A") (hol-light:fun (Tyvar "B") (Tyvar "A"))))))
!(ParameterOrderControl collector-distinct
  (== (hol-light:length (hol-light:term-type-vars (parameter-order:term))) 2))
!(ParameterOrderControl parameter-order
  (== (parameter-order:parameters) (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "B") HOL:Nil))))
!(ParameterOrderControl wrong-order
  (== (== (parameter-order:parameters) (HOL:Cons (Tyvar "B") (HOL:Cons (Tyvar "A") HOL:Nil))) False))

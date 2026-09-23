; A universal equality certificate justifies an alternative value computation.
; Membership remains an explicit argument; this does not authorize effects.
!(bind! &thy (new-space))
!(add-atom &thy (: A (u 0)))
!(add-atom &thy (: a A))
!(add-atom &thy (: b A))
!(set:define &thy copy (-> (A : (u 0)) (x : A) A)
   (= (copy $A $x) $x))
!(set:define &thy nestedCopy (-> (A : (u 0)) (x : A) A)
   (= (nestedCopy $A $x) (copy $A (copy $A $x))))
!(set:define &thy certifiedShortcut
   (-> (A : (u 0)) (B : (-> (z : A) (u 0)))
       (f g : (-> (z : A) (B z)))
       (Member : (-> (z : A) (u 0)))
       (theorem : (-> (z : A) (w : (Member z)) (id (B z) (f z) (g z))))
       (x : A) (member : (Member x))
       (sigma (result : (B x)) (id (B x) (f x) result)))
   (= (certifiedShortcut $A $B $f $g $Member $theorem $x $member)
      (pair ($g $x) ($theorem $x $member))))
; The theorem covers every value, not an enumerated list of successful cases.
!(type:check &thy (lam z (lam w (refl z)))
   (-> (z : A) (w : (id A z z))
       (id A (nestedCopy A z) (copy A z))))
!(type:check &thy
   (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
      (lam z (id A z z)) (lam z (lam w (refl z))) a (refl a))
   (sigma (result : A) (id A (nestedCopy A a) result)))
!(type:eq &thy
   (fst (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
      (lam z (id A z z)) (lam z (lam w (refl z))) a (refl a))) a)
!(type:check &thy
   (snd (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
      (lam z (id A z z)) (lam z (lam w (refl z))) a (refl a)))
   (id A (nestedCopy A a) a))
; Wrong dependent input membership and wrong theorem endpoints are refused.
!(type:check &thy
   (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
      (lam z (id A z z)) (lam z (lam w (refl z))) a (refl b))
   (sigma (result : A) (id A (nestedCopy A a) result)))
!(type:check &thy
   (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
      (lam z (id A z z)) (lam z (lam w (refl b))) a (refl a))
   (sigma (result : A) (id A (nestedCopy A a) result)))
!(type:check &thy
   (certifiedShortcut A (lam z A) (nestedCopy A) (lam z b)
      (lam z (id A z z)) (lam z (lam w (refl z))) a (refl a))
   (sigma (result : A) (id A (nestedCopy A a) result)))
; No certificate is fabricated when the universal theorem is unknown.
!(let $outcome (try (type:check &thy
     (certifiedShortcut A (lam z A) (nestedCopy A) (copy A)
        (lam z (id A z z)) unknownTheorem a (refl a))
     (sigma (result : A) (id A (nestedCopy A a) result))))
   (car-atom $outcome))

; The result family can genuinely depend on the argument.
!(type:check &thy
   (certifiedShortcut A (lam z (id A z z))
      (lam z (copy (id A z z) (refl z))) (lam z (refl z))
      (lam z (id A z z)) (lam z (lam w (refl (refl z)))) a (refl a))
   (sigma (result : (id A a a))
      (id (id A a a) (copy (id A a a) (refl a)) result)))
!(type:check &thy
   (certifiedShortcut A (lam z (id A z z))
      (lam z (copy (id A z z) (refl z))) (lam z (refl b))
      (lam z (id A z z)) (lam z (lam w (refl (refl z)))) a (refl a))
   (sigma (result : (id A a a))
      (id (id A a a) (copy (id A a a) (refl a)) result)))

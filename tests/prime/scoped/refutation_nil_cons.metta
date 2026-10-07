(set:profile hol)

; What a refutation at declared datatypes rests on.  The conversion
; algorithm refutes an equation when the two sides have different normal
; forms at their principal type; that rests on the algorithm being complete
; for the package, which Lean proves for the bare tower and the object
; package but not yet with declared datatypes, so such a refutation prints
; (assumes-completeness declared-datatypes ...).
;
; The empty list against a non-empty one needs no such assumption: Lean
; separates the two at the reading (CodeModel.nil_not_equal_cons: for
; closed a and l, no equality of nil and cons a l at the lists is
; derivable).  The refutation cites that theorem when the comparison is
; exactly that one, closed, over the numbers and lists declared as Lean
; declares them; every other refutation keeps its assumption.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))

; ---- The empty list is no non-empty list, on either side.
!(try (type:eq &self nil (cons zero nil)))
!(try (type:eq &self (cons (suc zero) (cons zero nil)) nil))
!(type:eq &self nil (cons zero nil))

; ---- Every other refutation keeps its assumption.
; A side that computes to a non-empty list through a definition: the
; refutation also rests on the definition's rules.
!(try (type:eq &self nil (append nil (cons zero nil))))
; Two non-empty lists, and two numbers: different constructors deeper in, or
; of another datatype.
!(try (type:eq &self (cons zero nil) (cons (suc zero) nil)))
!(try (type:eq &self zero (suc zero)))
; Open terms: the empty list and a non-empty one under a binder.
!(try (type:eq &self (lam (x : num) nil) (lam (x : num) (cons x nil))))
; Another datatype of the same shape under other names, and one with a third
; constructor: not the lists Lean declares.
!(set:inductive &self seq (u 0) (: snil seq) (: scons (-> num seq seq)))
!(try (type:eq &self snil (scons zero snil)))
!(set:inductive &self blist (u 0) (: bnil blist) (: bcons (-> num blist blist)) (: bextra blist))
!(try (type:eq &self bnil (bcons zero bnil)))
; Equal sides are not refuted.
!(try (type:eq &self (cons zero nil) (cons zero nil)))
; Lists of another element type: the same constructor names, but not the
; lists of numbers Lean declares.  The same lists of numbers in another space
; cite the theorem there.
!(bind! &bits (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &bits bit (u 0) (: b0 bit) (: b1 bit))
!(set:inductive &bits list (u 0) (: nil list) (: cons (-> bit list list)))
!(try (type:eq &bits nil (cons b0 nil)))
; Numbers with a third constructor: the lists over them are not Lean's
; either, though the two sides are written alike.
!(bind! &extended (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &extended num (u 0) (: zero num) (: suc (-> num num)) (: infinity num))
!(set:inductive &extended list (u 0) (: nil list) (: cons (-> num list list)))
!(try (type:eq &extended nil (cons zero nil)))
!(bind! &numbers (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &numbers num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &numbers list (u 0) (: nil list) (: cons (-> num list list)))
!(try (type:eq &numbers nil (cons (suc (suc zero)) nil)))
; The verdict follows the space: with nil also declared a number by a raw
; atom, the comparison is not decided; without it, the theorem is cited again.
!(add-atom &self (: nil num))
!(try (type:eq &self nil (cons zero nil)))
!(remove-atom &self (: nil num))
!(try (type:eq &self nil (cons zero nil)))

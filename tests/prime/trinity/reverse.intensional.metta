(set:profile hol)

; Two reversals of a list, typed.  The lists, append and the two reversals
; are declared in &self; the kernel checks each declaration before it admits
; it.  Then the type: language says what each is, which terms are equal by
; computation, and proves "rev l = rev-onto nil l" as a term of an identity
; type: equal by computation at every closed list, but not at an unknown
; list, where the two are different terms and the equality needs the proof.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))
!(set:define &self rev (-> list list)
   (= (rev nil) nil)
   (= (rev (cons $a $as)) (append (rev $as) (cons $a nil))))
!(set:define &self rev-onto (-> list list list)
   (= (rev-onto $acc nil) $acc)
   (= (rev-onto $acc (cons $x $xs)) (rev-onto (cons $x $acc) $xs)))

; ---- Why running ends.
; rev recurses on its argument: field 1 of cons, the tail.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev))
   (quote $termination))
; rev-onto recurses on its second argument, the list, and its recursive
; call changes the first, the accumulator.  The check admits it as written,
; not by declining it: it checks the equations as written, recursion on the
; tail of argument 1, then defines rev-onto~scrutinee-first by the same
; equations with the list first, and rev-onto as passing its arguments to it,
;   rev-onto acc l = rev-onto~scrutinee-first l acc.
; The record of rev-onto names that definition and argument 1; the record of
; the definition with the list first recurses on its argument 0.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev-onto))
   (quote $termination))
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev-onto~scrutinee-first))
   (quote $termination))
; Each record names the Lean theorem it rests on.  rev is structural
; recursion on its one argument, the shape Lean states.  rev-onto is one
; equation passing its arguments on, an explicit definition.  The definition
; with the list first is structural recursion of two arguments; Lean states
; recursion for a function of one, and the record says so.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev))
   (quote $lean))
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev-onto))
   (quote $lean))
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self rev-onto~scrutinee-first))
   (quote $lean))

; ---- What each is.
!(type:of &self rev)
!(type:of &self rev-onto)
!(type:of &self rev-onto~scrutinee-first)
!(type:check &self rev-onto (-> list list list))
; rev-onto of one list is a function from lists to lists; rev of a list is a
; list.  A number is not a list.
!(type:of &self (rev-onto nil))
!(type:check &self (rev (cons zero nil)) list)
!(try (type:of &self (rev-onto zero nil)))
!(type:check &self (rev-onto nil zero) list)

; ---- Equal by computation at closed lists.
!(type:eq &self (rev nil) (rev-onto nil nil))
!(type:eq &self (rev (cons zero (cons (suc zero) nil)))
   (rev-onto nil (cons zero (cons (suc zero) nil))))
!(type:eq &self (rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
   (rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil)))))
; Both compute to [2, 1, 0]; the reversal of [0, 1] is not [0, 1].
!(type:eq &self (rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
   (cons (suc (suc zero)) (cons (suc zero) (cons zero nil))))
!(type:eq &self (rev (cons zero (cons (suc zero) nil))) (cons zero (cons (suc zero) nil)))

; ---- Not equal by computation at an unknown list.
!(add-atom &self (: some-list list))
; rev some-list and rev-onto nil some-list do not compute: both look at the
; list.  They are different terms.
!(type:eq &self (rev some-list) (rev-onto nil some-list))
!(try (type:eq &self (rev some-list) (rev-onto nil some-list)))
; Under a binder the two functions are not equal by computation either, with
; the same verdict: the written domain fixes their type, and their bodies
; have different normal forms at the type list.
!(try (type:eq &self (lam (l : list) (rev l)) (lam (l : list) (rev-onto nil l))))
; At a list that starts with a known number each takes one step,
!(type:eq &self (rev (cons zero some-list)) (append (rev some-list) (cons zero nil)))
!(type:eq &self (rev-onto nil (cons zero some-list)) (rev-onto (cons zero nil) some-list))
; and an unknown accumulator does not stop rev-onto at a known list.
!(type:eq &self (rev-onto some-list nil) some-list)

; ---- The theorem as a typed proof.
; Congruence under "a number before", symmetry and transitivity of identity
; proofs, each by the identity eliminator.
!(set:define &self cong-cons
   (-> (a : num) (x : list) (y : list) (p : (id list x y))
       (id list (cons a x) (cons a y)))
   (= (cong-cons $a $x $y $p)
      (id:eliminate list $x
         (lam y2 (lam e (id list (cons $a $x) (cons $a y2))))
         (refl (cons $a $x)) $y $p)))
!(set:define &self id-sym
   (-> (x : list) (y : list) (p : (id list x y)) (id list y x))
   (= (id-sym $x $y $p)
      (id:eliminate list $x (lam y2 (lam e (id list y2 $x))) (refl $x) $y $p)))
!(set:define &self id-trans
   (-> (x : list) (y : list) (z : list) (p : (id list x y)) (q : (id list y z))
       (id list x z))
   (= (id-trans $x $y $z $p $q)
      (id:eliminate list $y (lam z2 (lam e (id list $x z2))) $p $z $q)))
; append l nil = l, and associativity, by recursion on the first list.
!(set:define &self append-nil
   (-> (l : list) (id list (append l nil) l))
   (= (append-nil nil) (refl nil))
   (= (append-nil (cons $a $as))
      (cong-cons $a (append $as nil) $as (append-nil $as))))
!(set:define &self append-assoc
   (-> (xs : list) (ys : list) (zs : list)
       (id list (append (append xs ys) zs) (append xs (append ys zs))))
   (= (append-assoc nil $ys $zs) (refl (append $ys $zs)))
   (= (append-assoc (cons $a $as) $ys $zs)
      (cong-cons $a (append (append $as $ys) $zs) (append $as (append $ys $zs))
         (append-assoc $as $ys $zs))))
; The lemma rev-onto acc l = append (rev l) acc, by recursion on l: at the
; empty list both sides compute to acc; at cons x xs the left side computes
; to rev-onto (cons x acc) xs, the hypothesis at the longer accumulator
; cons x acc, and associativity moves [x] into it.
!(set:define &self rev-onto-append
   (-> (l : list) (acc : list) (id list (rev-onto acc l) (append (rev l) acc)))
   (= (rev-onto-append nil $acc) (refl $acc))
   (= (rev-onto-append (cons $x $xs) $acc)
      (id-trans (rev-onto (cons $x $acc) $xs) (append (rev $xs) (cons $x $acc))
         (append (append (rev $xs) (cons $x nil)) $acc)
         (rev-onto-append $xs (cons $x $acc))
         (id-sym (append (append (rev $xs) (cons $x nil)) $acc)
            (append (rev $xs) (cons $x $acc))
            (append-assoc (rev $xs) (cons $x nil) $acc)))))
; The theorem: the lemma at the empty accumulator, then append-nil.
!(set:define &self rev-is-rev-onto
   (-> (l : list) (id list (rev l) (rev-onto nil l)))
   (= (rev-is-rev-onto $l)
      (id-sym (rev-onto nil $l) (rev $l)
         (id-trans (rev-onto nil $l) (append (rev $l) nil) (rev $l)
            (rev-onto-append $l nil) (append-nil (rev $l))))))
!(type:check &self rev-is-rev-onto (-> (l : list) (id list (rev l) (rev-onto nil l))))
; At the unknown list it proves what computation does not decide,
!(type:check &self (rev-is-rev-onto some-list)
   (id list (rev some-list) (rev-onto nil some-list)))
; and at a closed list it computes to reflexivity.
!(type:eq &self (rev-is-rev-onto (cons zero (cons (suc zero) nil)))
   (refl (cons (suc zero) (cons zero nil))))
; Wrong proofs are refused: the lemma whose step returns the hypothesis at
; the same accumulator proves the statement for the tail, not for the list,
!(try (set:define &self wrong-lemma
   (-> (l : list) (acc : list) (id list (rev-onto acc l) (append (rev l) acc)))
   (= (wrong-lemma nil $acc) (refl $acc))
   (= (wrong-lemma (cons $x $xs) $acc) (wrong-lemma $xs $acc))))
; and reflexivity alone does not prove the theorem.
!(type:check &self (lam (l : list) (refl (rev l)))
   (-> (l : list) (id list (rev l) (rev-onto nil l))))
